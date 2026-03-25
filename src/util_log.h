#pragma once

struct log_func
{
	const std::string_view context;
	const std::u8string_view context2;

	log_func(const std::string_view c, const std::u8string_view c2 = {}) : context(c), context2(c2)
	{
		if (!str::is_empty(context2))
		{
			df::log(context, str::format(u8"start {}"sv, context2));
		}
		else
		{
			df::log(context, u8"start"sv);
		}
	}

	~log_func()
	{
		if (!str::is_empty(context2))
		{
			df::log(context, str::format(u8"exit {}"sv, context2));
		}
		else
		{
			df::log(context, u8"exit"sv);
		}
	}
};
