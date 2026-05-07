#pragma once

struct log_func
{
	const std::string_view context;
	const std::string_view context2;

	log_func(const std::string_view c, const std::string_view c2 = {}) : context(c), context2(c2)
	{
		if (!str::is_empty(context2))
		{
			df::log(context, std::format("start {}", context2));
		}
		else
		{
			df::log(context, "start");
		}
	}

	~log_func()
	{
		if (!str::is_empty(context2))
		{
			df::log(context, std::format("exit {}", context2));
		}
		else
		{
			df::log(context, "exit");
		}
	}
};
