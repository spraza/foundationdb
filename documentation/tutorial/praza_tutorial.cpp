#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <execinfo.h>
#include <cstdlib>
#include "flow/DeterministicRandom.h"
#include "flow/FileIdentifier.h"
#include "flow/Platform.h"
#include "flow/flow.h"
#include <iostream>
#include <vector>
#include <elfutils/libdwfl.h>
#include <functional>
#include <cxxabi.h>
#include <memory>
#include <string>

#include <unordered_map>
#include <any>
#include <functional>
#include <typeindex>
#include <optional>
#include <memory>

namespace detail2 {
class any_key {
	struct Concept {
		virtual ~Concept() = default;
		virtual bool equals(const Concept& other) const = 0;
		virtual std::size_t hash() const = 0;
		virtual std::type_index type() const = 0;
	};

	template <typename T>
	struct model : Concept {
		T data;

		model(T d) : data(std::move(d)) {}

		bool equals(const Concept& other) const override {
			if (typeid(*this) != typeid(other))
				return false;
			return data == static_cast<const model&>(other).data;
		}

		std::size_t hash() const override { return std::hash<T>{}(data); }

		std::type_index type() const override { return typeid(T); }
	};

	std::shared_ptr<const Concept> self;

public:
	template <typename T>
	any_key(T value) : self(std::make_shared<model<T>>(std::move(value))) {}

	bool operator==(const any_key& other) const { return self->equals(*other.self); }

	std::size_t hash() const { return self->hash(); }

	std::type_index type() const { return self->type(); }
};

struct any_key_hash {
	std::size_t operator()(const any_key& k) const { return k.hash(); }
};
} // namespace detail2

template <typename KeyType, typename ValueType>
class FlexibleMap2 {
private:
	std::unordered_map<detail2::any_key, std::any, detail2::any_key_hash> map;

public:
	void insert(const KeyType& key, const ValueType& value) { map.emplace(detail2::any_key(key), std::any(value)); }

	std::optional<ValueType> get(const KeyType& key) const {
		auto it = map.find(detail2::any_key(key));
		if (it != map.end()) {
			return std::any_cast<ValueType>(it->second);
		}
		return std::nullopt;
	}

	bool erase(const KeyType& key) { return map.erase(detail2::any_key(key)) > 0; }

	size_t size() const { return map.size(); }

	void clear() { map.clear(); }
};

// Example usage:
#include <iostream>
#include <string>
#include <vector>

template <typename T>
struct MyType {
	T value;

	bool operator==(const MyType& other) const { return value == other.value; }
};

template <typename T>
struct MyOtherType {
	T data;
};

namespace std {
template <typename T>
struct hash<MyType<T>> {
	std::size_t operator()(const MyType<T>& k) const { return std::hash<T>{}(k.value); }
};
} // namespace std

int main() {
	FlexibleMap2<MyType<int>, MyOtherType<std::string>> myMap;

	myMap.insert(MyType<int>{ 5 }, MyOtherType<std::string>{ "five" });
	myMap.insert(MyType<int>{ 10 }, MyOtherType<std::string>{ "ten" });

	auto value = myMap.get(MyType<int>{ 5 });
	if (value) {
		std::cout << "Value for key 5: " << value->data << std::endl;
	}

	std::cout << "Map size: " << myMap.size() << std::endl;

	myMap.erase(MyType<int>{ 10 });
	std::cout << "Map size after erase: " << myMap.size() << std::endl;

	return 0;
}