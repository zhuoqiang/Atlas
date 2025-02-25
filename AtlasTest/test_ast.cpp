#include <iostream>
#include <chrono>


#include "helper.h"
#include <Eigen/Dense>

#pragma warning(suppress:5050)
import HydraModule;
import ExchangeModule;
import PortfolioModule;
import StrategyModule;
import TracerModule;

import AssetNodeModule;
import ExchangeNodeModule;
import StrategyNodeModule;
import HelperNodesModule;

using namespace Atlas;
using namespace Atlas::AST;

double asset1_close[] = {101,103,105,106};
double asset2_close[] = {101.5,99,97,101.5,101.5,96};

/*
				  Asset1		  Asset2
			   open   close    open   close
T0             NaN     NaN  101.00  101.50
T1           100.00  101.00  100.00   99.00
T2           102.00  103.00   98.00   97.00
T3           104.00  105.00  101.00  101.50
T4           105.00  106.00  101.00  101.50
T5             NaN     NaN  103.00   96.00
*/

class SimpleExchangeTests : public ::testing::Test
{
protected:
	std::shared_ptr<Atlas::Hydra> hydra;
	std::shared_ptr<Atlas::Portfolio> portfolio;
	std::string exchange_id = "test";
	std::string portfolio_id = "test_p";
	std::string strategy_id = "test_s";
	double initial_cash = 100.0f;
	size_t asset_id_1;
	size_t asset_id_2;

	void SetUp() override
	{
		hydra = std::make_shared<Hydra>();
		auto exchange = hydra->addExchange(exchange_id, exchange_path).value();
		asset_id_1 = exchange->getAssetIndex("asset1").value();
		asset_id_2 = exchange->getAssetIndex("asset2").value();
		portfolio = hydra->addPortfolio(portfolio_id, *exchange, initial_cash).value();
	}
};


class ComplexExchangeTests : public ::testing::Test
{
protected:
	std::shared_ptr<Atlas::Hydra> hydra;
	std::shared_ptr<Atlas::Portfolio> portfolio;
	std::shared_ptr<Atlas::Exchange> exchange_sptr;
	std::string exchange_id = "test";
	std::string portfolio_id = "test_p";
	std::string strategy_id = "test_s";
	double initial_cash = 100.0f;

	void SetUp() override
	{
		hydra = std::make_shared<Hydra>();
		exchange_sptr = hydra->addExchange(exchange_id, exchange_path_sp500).value();
		portfolio = hydra->addPortfolio(portfolio_id, *(exchange_sptr.get()), initial_cash).value();
	}
};


TEST_F(SimpleExchangeTests, BuildTest) 
{
	auto exchange_res = hydra->getExchange("test");
	EXPECT_TRUE(exchange_res.has_value());
	auto exchange = exchange_res.value();
	auto const& timestamps = exchange->getTimestamps();
	EXPECT_EQ(timestamps.size(), 6);
}


TEST_F(SimpleExchangeTests, ReadTest)
{
	auto const& exchange = hydra->getExchange("test").value();
	auto read_close_opt = AssetReadNode::make("close", 0, *exchange);
	EXPECT_TRUE(read_close_opt);
	auto read_close = std::move(read_close_opt.value());
	hydra->build();
	hydra->step();
	auto view = read_close->evaluate();
	EXPECT_EQ(view.rows(), 2);
	EXPECT_EQ(view.cols(), 1);
	EXPECT_TRUE(std::isnan(view(asset_id_1, 0)));
	EXPECT_DOUBLE_EQ(view(asset_id_2, 0), 101.5f);
	hydra->step();
	auto view2 = read_close->evaluate();
	EXPECT_DOUBLE_EQ(view2(asset_id_1, 0), 101.0f);
	EXPECT_DOUBLE_EQ(view2(asset_id_2, 0), 99.0f);
}

TEST_F(SimpleExchangeTests, ExchangeViewTest)
{
	auto const exchange = hydra->getExchange("test").value();
	auto read_close = AssetReadNode::make("close", 0, *exchange);
	auto read_close_previous = AssetReadNode::make("close", -1, *exchange);
	auto daily_return = AssetQuotientNode::make(
		std::move(*read_close),
		std::move(*read_close_previous)
	);
	auto op_variant = AssetOpNodeVariant(std::move(daily_return));
	auto exchange_view = ExchangeViewNode::make(*exchange, std::move(op_variant));
	hydra->build();
	hydra->step();
	hydra->step();
	Eigen::VectorXd buffer(2);
	exchange_view->evaluate(buffer);
	EXPECT_TRUE(std::isnan(buffer(0)));
	EXPECT_DOUBLE_EQ(buffer(1), (99 / 101.5));
}


TEST_F(SimpleExchangeTests, AllocTest)
{
	auto const exchange = hydra->getExchange("test").value();
	auto read_close = AssetReadNode::make("close", 0, *exchange);
	auto read_variant = AssetOpNodeVariant(std::move(*read_close));
	auto exchange_view = ExchangeViewNode::make(*exchange, std::move(read_variant));
	auto alloc = AllocationNode::make(std::move(exchange_view));
	auto strategy_node = StrategyNode::make(std::move(*alloc), *portfolio);
	auto strategy = std::make_unique<Strategy>(
		strategy_id,
		std::move(strategy_node),
		1.0f
	);
	hydra->build();
	auto res = hydra->addStrategy(std::move(strategy));
	EXPECT_TRUE(res);
	auto const& tracer = res.value()->getTracer();
	hydra->step();
	EXPECT_DOUBLE_EQ(tracer.getNLV(), initial_cash);
	hydra->step();
	double asset_2_return = (asset2_close[1] - asset2_close[0]) / asset2_close[0];
	double nlv = initial_cash * (1.0f + asset_2_return);
	EXPECT_DOUBLE_EQ(tracer.getNLV(), nlv);
	hydra->step();
	double asset_2_return2 = (asset2_close[2] - asset2_close[1]) / asset2_close[1];
	double asset_1_return2 = (asset1_close[1] - asset1_close[0]) / asset1_close[0];
	double avg_return = (.5f * asset_2_return2) + (.5f * asset_1_return2);
	nlv *= (1.0f + avg_return);
	EXPECT_DOUBLE_EQ(tracer.getNLV(), nlv);
}

TEST_F(SimpleExchangeTests, AllocSplitTest)
{
	auto const exchange = hydra->getExchange("test").value();
	auto read_close = AssetReadNode::make("close", 0, *exchange);
	auto read_close_previous = AssetReadNode::make("close", -1, *exchange);
	auto daily_return = AssetQuotientNode::make(
		std::move(*read_close),
		std::move(*read_close_previous)
	);
	auto op_variant = AssetOpNodeVariant(std::move(daily_return));
	auto exchange_view = ExchangeViewNode::make(*exchange, std::move(op_variant));
	auto alloc = AllocationNode::make(
		std::move(exchange_view),
		AllocationType::CONDITIONAL_SPLIT,
		1.0f
	);
	auto strategy_node = StrategyNode::make(std::move(*alloc), *portfolio);
	auto strategy = std::make_unique<Strategy>(
		strategy_id,
		std::move(strategy_node),
		1.0f
	);
	hydra->build();
	auto res = hydra->addStrategy(std::move(strategy));
	auto strategy_ptr = res.value();
	auto const& tracer = res.value()->getTracer();
	hydra->step();
	EXPECT_DOUBLE_EQ(tracer.getNLV(), initial_cash);
	hydra->step();
	EXPECT_DOUBLE_EQ(tracer.getNLV(), initial_cash);
	EXPECT_DOUBLE_EQ(strategy_ptr->getAllocation(asset_id_1), 0.0f);
	EXPECT_DOUBLE_EQ(strategy_ptr->getAllocation(asset_id_2), -1.0f);
	hydra->step();
	double avg_return = -1.0 * (asset2_close[2] - asset2_close[1]) / asset2_close[1];
	double nlv = initial_cash * (1.0f + avg_return);
	EXPECT_DOUBLE_EQ(tracer.getNLV(), nlv);
	EXPECT_DOUBLE_EQ(strategy_ptr->getAllocation(asset_id_1), .5f);
	EXPECT_DOUBLE_EQ(strategy_ptr->getAllocation(asset_id_2), -.5f);
	hydra->step();
	double asset_2_return2 = (asset2_close[3] - asset2_close[2]) / asset2_close[2];
	double asset_1_return2 = (asset1_close[2] - asset1_close[1]) / asset1_close[1];
	double avg_return2 = (-.5f * asset_2_return2) + (.5f * asset_1_return2);
	nlv *= (1.0f + avg_return2);
	EXPECT_DOUBLE_EQ(tracer.getNLV(), nlv);
}


TEST_F(ComplexExchangeTests, SimpleTest)
{
	auto now = std::chrono::system_clock::now();
	hydra->build();
	auto const exchange = hydra->getExchange("test").value();
	auto read_close = AssetReadNode::make("close", 0, *exchange);
	auto read_50_ma = AssetReadNode::make("50_ma", 0, *exchange);
	auto daily_return = AssetDifferenceNode::make(
		std::move(*read_close),
		std::move(*read_50_ma)
	);
	auto op_variant = AssetOpNodeVariant(std::move(daily_return));
	auto exchange_view = ExchangeViewNode::make(
		*exchange, std::move(op_variant)
	);
	exchange_view->setFilter(ExchangeViewFilterType::GREATER_THAN, 0.0f);
	auto alloc = AllocationNode::make(std::move(exchange_view));
	auto strategy_node = StrategyNode::make(std::move(*alloc), *portfolio);
	auto strategy = std::make_unique<Strategy>(
		strategy_id,
		std::move(strategy_node),
		1.0f
	);
	auto res = hydra->addStrategy(std::move(strategy));
	hydra->run();
	auto end = std::chrono::system_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - now);

	auto strategy_ptr = res.value();
	auto tracer = strategy_ptr->getTracer();
	auto nlv = tracer.getNLV();
	auto total_return = (nlv - initial_cash) / initial_cash;
	double epsilon = abs(total_return - 2.6207);
	EXPECT_TRUE(epsilon < 0.0005);
#ifdef _RELEASE
	EXPECT_TRUE(duration.count() < 500.0f);
#endif
	//std::cerr << "Duration: " << duration.count() << "s" << std::endl;
	//std::cerr << "Total Return: " << total_return << std::endl;
	//std::cerr << "Epsilon: " << epsilon << std::endl;
}



TEST_F(ComplexExchangeTests, FixedAllocTest)
{
	hydra->build();
	auto const exchange = hydra->getExchange("test").value();
	Vector<std::pair<String, double>> m_allocations = {
		{"msft", .5},
		{"amzn", .3},
		{"jnj",	 .2}
	};
	auto allocation_node = FixedAllocationNode::make(
		std::move(m_allocations),
		exchange,
		0.0
	);
	auto strategy_node = StrategyNode::make(
		std::move(*allocation_node),
		*portfolio
	);
	auto monthly_trigger_node = StrategyMonthlyRunnerNode::pyMake(
		exchange_sptr
	);
	strategy_node->setTrigger(std::move(monthly_trigger_node));
	auto strategy = std::make_unique<Strategy>(
		strategy_id,
		std::move(strategy_node),
		1.0f
	);
	auto res = hydra->addStrategy(std::move(strategy));
	EXPECT_TRUE(res);
	hydra->run();

	auto strategy_ptr = res.value();
	auto tracer = strategy_ptr->getTracer();
	auto nlv = tracer.getNLV();
	auto total_return = (nlv - initial_cash) / initial_cash;
	double epsilon = abs(total_return - 10.3586);
	EXPECT_TRUE(epsilon < 0.0005);
}