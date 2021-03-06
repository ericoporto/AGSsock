/*******************************************************
 * Test interface -- header file                       *
 *                                                     *
 * Author: Ferry "Wyz" Timmers                         *
 *                                                     *
 * Date: 15:51 2019-2-8                                *
 *                                                     *
 * Description: Interface to maintain unit tests       *
 *******************************************************/

#ifndef _TEST_H
#define _TEST_H

#include <functional>

//------------------------------------------------------------------------------

class Test
{
	struct Registry;

	public:
	using Body = const std::function<bool()>;

	const char *description;
	Body body;

	Test(const char *description, Body body);

	static bool run_tests();
	static bool mark(const char *file, int line);
};

#define EXPECT(x) do { if (!(x)) \
	return Test::mark(__FILE__, __LINE__); } while(false)

//------------------------------------------------------------------------------

#endif // _TEST_H

//..............................................................................
