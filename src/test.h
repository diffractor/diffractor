#pragma once

static void assert_equal(const std::string_view expected, const std::string_view actual,
                         const std::string_view name = {}, const std::string_view message = {})
{
	if (str::icmp(actual, expected) != 0)
	{
		throw test_assert_exception(
			std::format("{} - {}: expected '{}', got '{}'", message, name, expected, actual));
	}
}

static void assert_not_equal(const std::string_view expected, const std::string_view actual,
                             const std::string_view name = {}, const std::string_view message = {})
{
	if (str::icmp(actual, expected) == 0)
	{
		throw test_assert_exception(std::format("{} - {}: expected '{}' not to equal '{}'", message, name, expected,
		                                        actual));
	}
}

static void assert_equal_strict(const std::string_view expected, const std::string_view actual,
                                const std::string_view name = {}, const std::string_view message = {})
{
	if (actual != expected)
	{
		throw test_assert_exception(
			std::format("{} - {}: expected '{}', got '{}'", message, name, expected, actual));
	}
}

static void assert_equal(const std::wstring_view expected, const std::wstring_view actual,
                         const std::string_view name = {}, const std::string_view message = {})
{
	assert_equal(str::utf16_to_utf8(expected), str::utf16_to_utf8(actual), name, message);
}

static void assert_equal(const int expected, const int actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(const uint32_t expected, const uint32_t actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(const uint64_t expected, const uint64_t actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(const ui::const_image_ptr& expected, const ui::const_image_ptr& actual,
                         const std::string_view name = {}, const std::string_view message = {})
{
	assert_equal(static_cast<int>(expected->width()), static_cast<int>(actual->width()), name, message);

	files ff;
	const auto diff = ff.pixel_difference(expected, actual);
	assert_equal(true, diff == ui::pixel_difference_result::equal, name, message);
}

static void assert_equal(const bool expected, const bool actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}


static void assert_equal(const double expected, const double actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(str::to_string(expected, 5), str::to_string(actual, 5), name, message);
}

static void assert_equal(const df::date_t expected, const df::date_t actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(platform::format_date_time(expected), platform::format_date_time(actual), name, message);
}

static void assert_equal(const gps_coordinate expected, const gps_coordinate actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(gps_coordinate::decimal_to_dms_str(expected.latitude(), true),
	             gps_coordinate::decimal_to_dms_str(actual.latitude(), true), name, message);
	assert_equal(gps_coordinate::decimal_to_dms_str(expected.longitude(), false),
	             gps_coordinate::decimal_to_dms_str(actual.longitude(), false), name, message);
}

static void assert_equal(const df::xy8 expected, const df::xy8 actual, const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(expected.str(), actual.str(), name, message);
}
