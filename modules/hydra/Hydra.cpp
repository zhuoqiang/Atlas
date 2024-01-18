module;
#include <cassert>
#include "AtlasMacros.hpp"
#include <stdexcept>
module HydraModule;

import ExchangeMapModule;
import PortfolioModule;
import StrategyModule;

namespace Atlas
{

//============================================================================
struct HydraImpl
{
	ExchangeMap m_exchange_map;
	Vector<SharedPtr<Strategy>> m_strategies;
	Vector<SharedPtr<Portfolio>> m_portfolios;
	HashMap<String, size_t> m_strategy_map;
	HashMap<String, size_t> m_portfolio_map;
};


//============================================================================
Hydra::Hydra() noexcept
{
	m_impl = std::make_unique<HydraImpl>();
}


//============================================================================
Hydra::~Hydra() noexcept
{
}


//============================================================================
Result<SharedPtr<Exchange>, AtlasException>
Hydra::addExchange(String name, String source) noexcept
{
	return m_impl->m_exchange_map.addExchange(std::move(name), std::move(source));
}


//============================================================================
Result<Exchange*, AtlasException>
Hydra::getExchange(String const& name) const noexcept
{
	return m_impl->m_exchange_map.getExchange(name);
}


//============================================================================
Result<Strategy const*, AtlasException>
Hydra::addStrategy(SharedPtr<Strategy> strategy) noexcept
{
	if (m_state == HydraState::RUNING)
	{
		return Err("Hydra can not be in running to add strategy");
	}
	if (m_impl->m_strategy_map.contains(strategy->getName()))
	{
		return Err("Strategy with name " + strategy->getName() + " already exists");
	}
	strategy->setID(m_impl->m_strategies.size());
	m_impl->m_strategies.push_back(std::move(strategy));
	return m_impl->m_strategies.back().get();
}


//============================================================================
Result<SharedPtr<Portfolio>, AtlasException>
Hydra::addPortfolio(String name, Exchange& exchange, double initial_cash) noexcept
{
	if (m_state != HydraState::INIT)
	{
		return Err("Hydra must be in init state to add portfolio");
	}
	if (m_impl->m_portfolio_map.contains(name))
	{
		return Err("Portfolio with name " + name + " already exists");
	}
	auto portfolio = std::make_shared<Portfolio>(
		std::move(name), m_impl->m_portfolio_map.size(), exchange, initial_cash
	);	
	m_impl->m_portfolios.push_back(portfolio);
	m_impl->m_portfolio_map[portfolio->getName()] = portfolio->getId();
	return portfolio;
}


//============================================================================
Result<bool, AtlasException>
Hydra::build()
{
	m_impl->m_exchange_map.build();
	m_state = HydraState::BUILT;
	return true;
}


//============================================================================
void
Hydra::step() noexcept
{
	assert(m_state == HydraState::BUILT || m_state == HydraState::RUNING);
	m_impl->m_exchange_map.step();
	
	for (int i = 0; i < m_impl->m_strategies.size(); i++)
	{
		m_impl->m_strategies[i]->evaluate();
		m_impl->m_strategies[i]->step();
	}
}



//============================================================================
void
Hydra::run() noexcept
{
	assert(m_state == HydraState::BUILT);
	size_t steps = m_impl->m_exchange_map.getTimestamps().size();
	for (size_t i = 0; i < steps; ++i)
	{
		step();
	}
}


//============================================================================
Result<bool, AtlasException>
Hydra::reset() noexcept
{
	if (m_state == HydraState::INIT)
	{
		return Err("Hydra can not be in init state to reset");
	}
	m_impl->m_exchange_map.reset();
	for (auto& strategy : m_impl->m_strategies)
	{
		strategy->reset();
	}
	return true;
}


//============================================================================
SharedPtr<Exchange>
Hydra::pyAddExchange(String name, String source)
{
	auto res = addExchange(std::move(name), std::move(source));
	if (!res)
	{
		throw std::exception(res.error().what());
	}
	return *res;
}


//============================================================================
SharedPtr<Portfolio>
Hydra::pyAddPortfolio(String name, SharedPtr<Exchange> exchange, double intial_cash)
{
	auto res = addPortfolio(std::move(name), *(exchange.get()), intial_cash);
	if (!res)
	{
		throw std::exception(res.error().what());
	}
	return *res;
}


//============================================================================
SharedPtr<Strategy>
Hydra::pyAddStrategy(SharedPtr<Strategy> strategy)
{
	auto res = addStrategy(std::move(strategy));
	if (!res)
	{
		throw std::exception(res.error().what());
	}
	return m_impl->m_strategies.back();
}


//============================================================================
void
Hydra::pyBuild()
{
	auto res = build();
	if (!res)
	{
		throw std::exception(res.error().what());
	}
}


//============================================================================
void Hydra::pyReset()
{
	auto res = reset();
	if (!res)
	{
		throw std::exception(res.error().what());
	}
}


}