// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Extension.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

namespace {

class FakeHost final : public EffectHost {
public:
	HostServices& services() override { throw OfxGripException(Error::make(ErrorCode::Unsupported, "no services")); }
	EventBus& bus() override { return bus_; }
	const EffectManifest& manifest() const override { return manifest_; }

private:
	EventBus bus_;
	EffectManifest manifest_;
};

class Counter final : public Extension {
public:
	std::string name() const override { return "Counter"; }
	std::vector<EventId> interests() const override { return {EventId::Flush, EventId::Render}; }
	void handle(Event& e) override { seen.push_back(e.id); }
	std::vector<EventId> seen;
};

class Individual final : public Extension {
public:
	std::string name() const override { return "Individual"; }
	std::vector<EventId> interests() const override { return {}; }
	void handle(Event&) override {}
	bool individuate() const override { return true; }
};

class Declining final : public Extension {
public:
	std::string name() const override { return "Declining"; }
	std::vector<EventId> interests() const override { return {}; }
	void handle(Event&) override {}
	bool load(EffectHost&, int) override { return false; }
};

class Early final : public Extension {
public:
	std::string name() const override { return "Early"; }
	std::vector<EventId> interests() const override { return {EventId::Flush}; }
	int priority(EventId) const override { return kPriorityFirst; }
	void handle(Event&) override { (*order).push_back("early"); }
	std::vector<std::string>* order{nullptr};
};

class Late final : public Extension {
public:
	std::string name() const override { return "Late"; }
	std::vector<EventId> interests() const override { return {EventId::Flush}; }
	void handle(Event&) override { (*order).push_back("late"); }
	std::vector<std::string>* order{nullptr};
};

}  // namespace

TEST(Extension, SubscribesInterestsAndReceivesEvents)
{
	FakeHost host;
	ExtensionSystem system(host);
	auto* counter = static_cast<Counter*>(system.load(std::make_unique<Counter>()));
	ASSERT_NE(counter, nullptr);
	EXPECT_EQ(counter->key(), "Counter");
	system.dispatch(EventId::Flush);
	system.dispatch(EventId::Render);
	system.dispatch(EventId::Unload);
	EXPECT_EQ(counter->seen, (std::vector<EventId>{EventId::Flush, EventId::Render}));
	EXPECT_EQ(system.find("Counter"), counter);
	EXPECT_EQ(system.find<Counter>(), counter);
}

TEST(Extension, SecondInstanceGetsAnIndexedKey)
{
	FakeHost host;
	ExtensionSystem system(host);
	Extension* first = system.load(std::make_unique<Counter>());
	Extension* second = system.load(std::make_unique<Counter>());
	EXPECT_EQ(first->key(), "Counter");
	EXPECT_EQ(second->key(), "Counter1");
	EXPECT_EQ(system.extensions().size(), 2u);
	Extension* individual = system.load(std::make_unique<Individual>());
	EXPECT_EQ(individual->key(), "Individual0");
}

TEST(Extension, DecliningExtensionIsDropped)
{
	FakeHost host;
	ExtensionSystem system(host);
	EXPECT_EQ(system.load(std::make_unique<Declining>()), nullptr);
	EXPECT_TRUE(system.extensions().empty());
	EXPECT_EQ(system.load(nullptr), nullptr);
}

TEST(Extension, InactiveExtensionSubscribesNothing)
{
	FakeHost host;
	ExtensionSystem system(host);
	auto counter = std::make_unique<Counter>();
	counter->setActive(false);
	auto* raw = static_cast<Counter*>(system.load(std::move(counter)));
	system.dispatch(EventId::Flush);
	EXPECT_TRUE(raw->seen.empty());
	EXPECT_EQ(host.bus().handlerCount(EventId::Flush), 0u);
}

TEST(Extension, PriorityOrdersDelivery)
{
	FakeHost host;
	ExtensionSystem system(host);
	std::vector<std::string> order;
	auto late = std::make_unique<Late>();
	late->order = &order;
	auto early = std::make_unique<Early>();
	early->order = &order;
	system.load(std::move(late));
	system.load(std::move(early));
	system.dispatch(EventId::Flush);
	EXPECT_EQ(order, (std::vector<std::string>{"early", "late"}));
}

TEST(Extension, UnloadAllDropsSubscriptions)
{
	FakeHost host;
	ExtensionSystem system(host);
	system.load(std::make_unique<Counter>());
	EXPECT_EQ(host.bus().handlerCount(EventId::Flush), 1u);
	system.unloadAll();
	EXPECT_EQ(host.bus().handlerCount(EventId::Flush), 0u);
	EXPECT_TRUE(system.extensions().empty());
	EXPECT_EQ(system.find("Counter"), nullptr);
}
