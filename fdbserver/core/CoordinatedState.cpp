/*
 * CoordinatedState.cpp
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2026 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fdbclient/ClusterConnectionMemoryRecord.h"
#include "fdbclient/WellKnownEndpoints.h"
#include "fdbserver/core/CoordinatedState.h"
#include "fdbserver/core/CoordinationInterface.h"
#include "fdbserver/core/Knobs.h"
#include "flow/ActorCollection.h"
#include "fdbserver/core/LeaderElection.h"
#include "fdbserver/core/PaxosSequence.h"
#include "flow/CoroUtils.h"

struct CoordinatedStateImpl {
	ServerCoordinators coordinators;
	int stage{ 0 };
	uint64_t conflictGen{ 0 };
	Optional<PaxosSequenceReservation> reservation;

	explicit CoordinatedStateImpl(ServerCoordinators const& c) : coordinators(c) {}
	uint64_t getConflict() const { return conflictGen; }

	Future<Value> read() {
		ASSERT(stage == 0);
		stage = 1;
		reservation = co_await paxosSequenceRead(
		    coordinators.stateServers, coordinators.clusterKey, deterministicRandom()->randomUniqueID());
		conflictGen = reservation.get().instance;
		stage = 4;
		co_return reservation.get().currentValue;
	}

	Future<Void> onConflict() {
		ASSERT(stage == 4);
		ASSERT(reservation.present());
		co_await paxosSequenceOnConflict(
		    coordinators.stateServers, reservation.get(), SERVER_KNOBS->COORDINATED_STATE_ONCONFLICT_POLL_INTERVAL);
		if (stage == 4) {
			co_return;
		}
		co_await Future<Void>(Never());
	}

	Future<Void> setExclusive(Value v) {
		ASSERT(stage == 4);
		ASSERT(reservation.present());
		stage = 5;

		PaxosAcceptQuorum result = co_await paxosSequenceSet(coordinators.stateServers, reservation.get(), v);
		stage = 6;

		TraceEvent("CoordinatedStateSetPaxos")
		    .detail("Instance", reservation.get().instance)
		    .detail("BallotRound", reservation.get().ballot.round)
		    .detail("Proposer", reservation.get().ballot.proposer)
		    .detail("Chosen", result.chosen)
		    .detail("HighestPromisedRound", result.highestPromised.round);

		if (!result.chosen) {
			conflictGen = std::max(conflictGen, result.highestPromised.round);
			throw coordinated_state_conflict();
		}
	}
};

CoordinatedState::CoordinatedState(ServerCoordinators const& coord)
  : impl(PImpl<CoordinatedStateImpl>::create(coord)) {}
CoordinatedState::~CoordinatedState() = default;
Future<Value> CoordinatedState::read() {
	return impl->read();
}
Future<Void> CoordinatedState::onConflict() {
	return impl->onConflict();
}
Future<Void> CoordinatedState::setExclusive(Value v) {
	return impl->setExclusive(v);
}
uint64_t CoordinatedState::getConflict() const {
	return impl->getConflict();
}

struct MovableValue {
	enum MoveState { MaybeTo = 1, Active = 2, MovingFrom = 3 };

	Value value;
	int32_t mode;
	Optional<Value> other; // a cluster connection string

	MovableValue() : mode(Active) {}
	MovableValue(Value const& v, int mode, Optional<Value> other = Optional<Value>())
	  : value(v), mode(mode), other(other) {}

	// To change this serialization, ProtocolVersion::MovableCoordinatedStateV2 must be updated, and downgrades need to
	// be considered
	template <class Ar>
	void serialize(Ar& ar) {
		ASSERT(ar.protocolVersion().hasMovableCoordinatedState());
		serializer(ar, value, mode, other);
	}
};

struct MovableCoordinatedStateImpl {
	ServerCoordinators coordinators;
	CoordinatedState cs;
	Optional<Value> lastValue, // The value passed to setExclusive()
	    lastCSValue; // The value passed to cs.setExclusive()

	explicit MovableCoordinatedStateImpl(ServerCoordinators const& c) : coordinators(c), cs(c) {}

	Future<Value> read() {
		MovableValue moveState;
		Value rawValue = co_await cs.read();
		if (!rawValue.empty()) {
			BinaryReader r(rawValue, IncludeVersion());
			if (!r.protocolVersion().hasMovableCoordinatedState()) {
				// Old coordinated state, not a MovableValue
				moveState.value = rawValue;
			} else {
				r >> moveState;
			}
		}
		// SOMEDAY: If moveState.mode == MovingFrom, read (without locking) old state and assert that it corresponds
		// with our state and is ReallyTo(coordinators)
		if (moveState.mode == MovableValue::MaybeTo) {
			CODE_PROBE(true, "Maybe moveto state");
			ASSERT(moveState.other.present());
			co_await moveTo(&cs, ClusterConnectionString(moveState.other.get().toString()), moveState.value);
		}
		co_return moveState.value;
	}

	Future<Void> onConflict() { return cs.onConflict(); }

	Future<Void> setExclusive(Value v) {
		lastValue = v;
		lastCSValue = BinaryWriter::toValue(MovableValue(v, MovableValue::Active),
		                                    IncludeVersion(ProtocolVersion::withMovableCoordinatedStateV2()));
		return cs.setExclusive(lastCSValue.get());
	}

	Future<Void> move(ClusterConnectionString nc) {
		// Call only after setExclusive returns.  Attempts to move the coordinated state
		// permanently to the new ServerCoordinators, which must be uninitialized.  Returns when the process has
		// reached the point where a leader elected by the new coordinators should be doing the rest of the work
		// (and therefore the caller should die).
		CoordinatedState cs(coordinators);
		CoordinatedState nccs(ServerCoordinators(makeReference<ClusterConnectionMemoryRecord>(nc)));
		Future<Void> creationTimeout = delay(30);
		ASSERT(lastValue.present() && lastCSValue.present());
		TraceEvent("StartMove").detail("ConnectionString", nc.toString());
		{
			auto res = co_await race(creationTimeout, nccs.read());
			if (res.index() == 0) {
				throw new_coordinators_timed_out();
			}
			ASSERT(res.index() == 1);
			Value ncInitialValue = std::get<1>(std::move(res));
			ASSERT(ncInitialValue.empty()); // The new coordinators must be uninitialized!
		}
		TraceEvent("FinishedRead").detail("ConnectionString", nc.toString());

		{
			auto res = co_await race(
			    creationTimeout,
			    nccs.setExclusive(BinaryWriter::toValue(
			        MovableValue(
			            lastValue.get(), MovableValue::MovingFrom, coordinators.ccr->getConnectionString().toString()),
			        IncludeVersion(ProtocolVersion::withMovableCoordinatedStateV2()))));
			if (res.index() == 0) {
				throw new_coordinators_timed_out();
			}
			ASSERT(res.index() == 1);
		}

		if (buggify())
			co_await delay(5);

		Value oldQuorumState = co_await cs.read();
		if (oldQuorumState != lastCSValue.get()) {
			CODE_PROBE(
			    true, "Quorum change aborted by concurrent write to old coordination state", probe::decoration::rare);
			TraceEvent("QuorumChangeAbortedByConcurrency").log();
			throw coordinated_state_conflict();
		}

		co_await moveTo(&cs, nc, lastValue.get());

		throw coordinators_changed();
	}

	Future<Void> moveTo(CoordinatedState* coordinatedState, ClusterConnectionString nc, Value value) {
		co_await coordinatedState->setExclusive(
		    BinaryWriter::toValue(MovableValue(value, MovableValue::MaybeTo, nc.toString()),
		                          IncludeVersion(ProtocolVersion::withMovableCoordinatedStateV2())));

		if (buggify())
			co_await delay(5);

		// SOMEDAY: If we are worried about someone magically getting the new cluster ID and interfering, do a second
		// cs.setExclusive( encode( ReallyTo, ... ) )
		TraceEvent("ChangingQuorum").detail("ConnectionString", nc.toString());
		co_await changeLeaderCoordinators(coordinators, StringRef(nc.toString()));
		TraceEvent("ChangedQuorum").detail("ConnectionString", nc.toString());
		throw coordinators_changed();
	}
};

MovableCoordinatedState& MovableCoordinatedState::operator=(MovableCoordinatedState&&) = default;
MovableCoordinatedState::MovableCoordinatedState(class ServerCoordinators const& coord)
  : impl(PImpl<MovableCoordinatedStateImpl>::create(coord)) {}
MovableCoordinatedState::~MovableCoordinatedState() = default;
Future<Value> MovableCoordinatedState::read() {
	return impl->read();
}
Future<Void> MovableCoordinatedState::onConflict() {
	return impl->onConflict();
}
Future<Void> MovableCoordinatedState::setExclusive(Value v) {
	return impl->setExclusive(v);
}
Future<Void> MovableCoordinatedState::move(ClusterConnectionString const& nc) {
	return impl->move(nc);
}

Optional<Value> updateCCSInMovableValue(ValueRef movableVal, KeyRef oldClusterKey, KeyRef newClusterKey) {
	Optional<Value> result;
	auto moveVal = BinaryReader::fromStringRef<MovableValue>(
	    movableVal, IncludeVersion(ProtocolVersion::withMovableCoordinatedStateV2()));
	if (moveVal.other.present() && moveVal.other.get().startsWith(oldClusterKey)) {
		TraceEvent(SevDebug, "UpdateCCSInMovableValue").detail("OldConnectionString", moveVal.other.get());
		moveVal.other = moveVal.other.get().removePrefix(oldClusterKey).withPrefix(newClusterKey);
		result = BinaryWriter::toValue(moveVal, IncludeVersion(ProtocolVersion::withMovableCoordinatedStateV2()));
	}
	return result;
}
