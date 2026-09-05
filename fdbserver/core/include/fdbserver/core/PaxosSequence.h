/*
 * PaxosSequence.h
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

#ifndef FDBSERVER_PAXOS_SEQUENCE_H
#define FDBSERVER_PAXOS_SEQUENCE_H
#pragma once

#include "fdbserver/core/SingleDecreePaxos.h"

struct PaxosSequenceReservation {
	Key key;
	Value currentValue;
	uint64_t instance{ 0 };
	PaxosBallot ballot;
	bool initial{ false };
};

// Use a fresh proposer UID for each reservation attempt. A reservation may set
// only one value; an unsuccessful set can still be adopted by a later reader.
Future<PaxosSequenceReservation> paxosSequenceRead(std::vector<PaxosAcceptorInterface> const& acceptors,
                                                   Key key,
                                                   UID proposer);

Future<PaxosAcceptQuorum> paxosSequenceSet(std::vector<PaxosAcceptorInterface> const& acceptors,
                                           PaxosSequenceReservation const& reservation,
                                           Value value);

Future<Void> paxosSequenceOnConflict(std::vector<PaxosAcceptorInterface> const& acceptors,
                                     PaxosSequenceReservation reservation,
                                     double pollInterval);

#endif
