// SPDX-License-Identifier: MIT
// Copyright © 2026 Belisoful
#include <ofxgrip/core/Events.hpp>

#include <gtest/gtest.h>

using namespace ofxgrip;

TEST(Events, DeliversInAscendingPriority)
{
	EventBus bus;
	std::vector<int> order;
	Subscription a = bus.subscribe(EventId::Flush, kPriorityDefault, [&](Event&) { order.push_back(10); });
	Subscription b = bus.subscribe(EventId::Flush, kPriorityFirst, [&](Event&) { order.push_back(-20); });
	Subscription c = bus.subscribe(EventId::Flush, kPriorityLast, [&](Event&) { order.push_back(20); });
	PlainEvent event(EventId::Flush);
	EXPECT_TRUE(bus.post(event).ok());
	EXPECT_EQ(order, (std::vector<int>{-20, 10, 20}));
	order.clear();
	bus.post(event, true);
	EXPECT_EQ(order, (std::vector<int>{20, 10, -20}));
}

TEST(Events, EqualPrioritiesKeepSubscriptionOrder)
{
	EventBus bus;
	std::vector<int> order;
	Subscription a = bus.subscribe(EventId::Render, 5, [&](Event&) { order.push_back(1); });
	Subscription b = bus.subscribe(EventId::Render, 5, [&](Event&) { order.push_back(2); });
	PlainEvent event(EventId::Render);
	bus.post(event);
	EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(Events, VetoStopsDelivery)
{
	EventBus bus;
	int calls = 0;
	Subscription a = bus.subscribe(EventId::ApiSetValuePre, 0, [&](Event& e) {
		++calls;
		e.error = Error::make(ErrorCode::Value, "rejected");
	});
	Subscription b = bus.subscribe(EventId::ApiSetValuePre, 1, [&](Event&) { ++calls; });
	ApiEvent event(EventId::ApiSetValuePre, 7);
	Error result = bus.post(event);
	EXPECT_EQ(result.code(), ErrorCode::Value);
	EXPECT_EQ(calls, 1);
	EXPECT_TRUE(event.vetoed());
}

TEST(Events, OverrideResultIsCarried)
{
	EventBus bus;
	Subscription a = bus.subscribe(EventId::ApiGetValue, 0, [&](Event& e) { e.result = Value(42); });
	ApiEvent event(EventId::ApiGetValue, 1);
	bus.post(event);
	ASSERT_TRUE(event.result.has_value());
	EXPECT_EQ(event.result->toInt(), 42);
}

TEST(Events, UnsubscribeOnDestroy)
{
	EventBus bus;
	int calls = 0;
	{
		Subscription s = bus.subscribe(EventId::Flush, 0, [&](Event&) { ++calls; });
		EXPECT_EQ(bus.handlerCount(EventId::Flush), 1u);
	}
	EXPECT_EQ(bus.handlerCount(EventId::Flush), 0u);
	PlainEvent event(EventId::Flush);
	bus.post(event);
	EXPECT_EQ(calls, 0);
}

TEST(Events, SubscriptionMovesAndResets)
{
	EventBus bus;
	Subscription s = bus.subscribe(EventId::Flush, 0, [](Event&) {});
	Subscription moved = std::move(s);
	EXPECT_FALSE(s.active());
	EXPECT_TRUE(moved.active());
	moved.reset();
	EXPECT_FALSE(moved.active());
	EXPECT_EQ(bus.handlerCount(EventId::Flush), 0u);
}

TEST(Events, HandlerMayUnsubscribeDuringDelivery)
{
	EventBus bus;
	Subscription s;
	int calls = 0;
	s = bus.subscribe(EventId::Flush, 0, [&](Event&) {
		++calls;
		s.reset();
	});
	PlainEvent event(EventId::Flush);
	bus.post(event);
	bus.post(event);
	EXPECT_EQ(calls, 1);
}

TEST(Events, PayloadTypes)
{
	std::vector<ParameterSpec> specs;
	ManifestEvent m(EventId::AddParameters, specs);
	m.parameters.push_back(ParameterSpec::floatParam(1, "a", "A", 0, 0, 1));
	EXPECT_EQ(specs.size(), 1u);
	ChangeEvent c(EventId::InstanceChanged, 5, "gain", 3.0, ChangeReason::PluginEdited);
	EXPECT_EQ(c.parameter, 5u);
	EXPECT_EQ(c.reason, ChangeReason::PluginEdited);
	EXPECT_EQ(toString(EventId::InstanceChanged), "InstanceChanged");
	EXPECT_EQ(toString(EventId::ApiSetOptions), "ApiSetOptions");
	for (int i = 0; i < static_cast<int>(EventId::Count); ++i) {
		EXPECT_NE(toString(static_cast<EventId>(i)), "Unknown");
	}
}
