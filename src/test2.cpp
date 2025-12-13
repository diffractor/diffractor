#include "pch.h"
#include "model.h"
#include "model_db.h"
#include "view_test.h"
#include "test.h"

static void should_icmp_natural()
{
	// Test basic numeric comparison - the key bug fix
	// Files like 43_100 should come after 43_99, not between 43_10 and 43_11
	assert_equal(true, str::icmp_natural(u8"43_09"sv, u8"43_10"sv) < 0, u8"43_09 < 43_10"sv);
	assert_equal(true, str::icmp_natural(u8"43_10"sv, u8"43_11"sv) < 0, u8"43_10 < 43_11"sv);
	assert_equal(true, str::icmp_natural(u8"43_10"sv, u8"43_100"sv) < 0, u8"43_10 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_99"sv, u8"43_100"sv) < 0, u8"43_99 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_100"sv, u8"43_101"sv) < 0, u8"43_100 < 43_101"sv);
	
	// Verify the order reported in the bug is fixed
	assert_equal(true, str::icmp_natural(u8"43_09"sv, u8"43_100"sv) < 0, u8"43_09 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_11"sv, u8"43_100"sv) < 0, u8"43_11 < 43_100"sv);

	// Test equality
	assert_equal(0, str::icmp_natural(u8"file10"sv, u8"file10"sv), u8"equal strings"sv);
	assert_equal(0, str::icmp_natural(u8""sv, u8""sv), u8"empty strings"sv);

	// Test case insensitivity
	assert_equal(0, str::icmp_natural(u8"File10"sv, u8"file10"sv), u8"case insensitive"sv);
	assert_equal(0, str::icmp_natural(u8"FILE10"sv, u8"file10"sv), u8"case insensitive upper"sv);

	// Test basic natural ordering
	assert_equal(true, str::icmp_natural(u8"file1"sv, u8"file2"sv) < 0, u8"file1 < file2"sv);
	assert_equal(true, str::icmp_natural(u8"file2"sv, u8"file10"sv) < 0, u8"file2 < file10"sv);
	assert_equal(true, str::icmp_natural(u8"file9"sv, u8"file10"sv) < 0, u8"file9 < file10"sv);
	assert_equal(true, str::icmp_natural(u8"file10"sv, u8"file11"sv) < 0, u8"file10 < file11"sv);
	assert_equal(true, str::icmp_natural(u8"file19"sv, u8"file20"sv) < 0, u8"file19 < file20"sv);
	assert_equal(true, str::icmp_natural(u8"file99"sv, u8"file100"sv) < 0, u8"file99 < file100"sv);

	// Test reverse ordering
	assert_equal(true, str::icmp_natural(u8"file10"sv, u8"file9"sv) > 0, u8"file10 > file9"sv);
	assert_equal(true, str::icmp_natural(u8"file100"sv, u8"file99"sv) > 0, u8"file100 > file99"sv);

	// Test with different prefixes
	assert_equal(true, str::icmp_natural(u8"a10"sv, u8"b1"sv) < 0, u8"a10 < b1"sv);
	assert_equal(true, str::icmp_natural(u8"img001"sv, u8"img002"sv) < 0, u8"img001 < img002"sv);
	assert_equal(true, str::icmp_natural(u8"img009"sv, u8"img010"sv) < 0, u8"img009 < img010"sv);
	
	// Test numbers at the start
	assert_equal(true, str::icmp_natural(u8"1file"sv, u8"2file"sv) < 0, u8"1file < 2file"sv);
	assert_equal(true, str::icmp_natural(u8"9file"sv, u8"10file"sv) < 0, u8"9file < 10file"sv);
	assert_equal(true, str::icmp_natural(u8"10file"sv, u8"100file"sv) < 0, u8"10file < 100file"sv);

	// Test multiple number groups
	assert_equal(true, str::icmp_natural(u8"file1-1"sv, u8"file1-2"sv) < 0, u8"file1-1 < file1-2"sv);
	assert_equal(true, str::icmp_natural(u8"file1-9"sv, u8"file1-10"sv) < 0, u8"file1-9 < file1-10"sv);
	assert_equal(true, str::icmp_natural(u8"file1-10"sv, u8"file2-1"sv) < 0, u8"file1-10 < file2-1"sv);

	// Test leading zeros
	assert_equal(true, str::icmp_natural(u8"file007"sv, u8"file7"sv) > 0, u8"file007 > file7 (more leading zeros)"sv);
	assert_equal(true, str::icmp_natural(u8"file07"sv, u8"file007"sv) < 0, u8"file07 < file007 (fewer leading zeros)"sv);
	assert_equal(0, str::icmp_natural(u8"file007"sv, u8"file007"sv), u8"same with leading zeros"sv);

	// Test purely numeric strings
	assert_equal(true, str::icmp_natural(u8"1"sv, u8"2"sv) < 0, u8"1 < 2"sv);
	assert_equal(true, str::icmp_natural(u8"9"sv, u8"10"sv) < 0, u8"9 < 10"sv);
	assert_equal(true, str::icmp_natural(u8"99"sv, u8"100"sv) < 0, u8"99 < 100"sv);
	assert_equal(true, str::icmp_natural(u8"999"sv, u8"1000"sv) < 0, u8"999 < 1000"sv);

	// Test strings with no numbers
	assert_equal(true, str::icmp_natural(u8"abc"sv, u8"abd"sv) < 0, u8"abc < abd"sv);
	assert_equal(true, str::icmp_natural(u8"abc"sv, u8"abcd"sv) < 0, u8"abc < abcd"sv);
	assert_equal(0, str::icmp_natural(u8"abc"sv, u8"ABC"sv), u8"abc == ABC (case insensitive)"sv);

	// Test image sequence patterns (common use case)
	assert_equal(true, str::icmp_natural(u8"DSC_0001.jpg"sv, u8"DSC_0002.jpg"sv) < 0, u8"DSC sequence"sv);
	assert_equal(true, str::icmp_natural(u8"DSC_0099.jpg"sv, u8"DSC_0100.jpg"sv) < 0, u8"DSC sequence 99-100"sv);
	assert_equal(true, str::icmp_natural(u8"IMG_9999.png"sv, u8"IMG_10000.png"sv) < 0, u8"IMG sequence overflow"sv);
	assert_equal(true, str::icmp_natural(u8"IMG_9999.png"sv, u8"IMG_10000.png"sv) < 0, u8"IMG sequence overflow"sv);
}

void register_tests2(view_state& state, test_registry& tests)
{
	tests.add(u8"Should natural compare"s, should_icmp_natural);
}