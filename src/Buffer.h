/*******************************************************
 * Data buffer class -- header file                    *
 *                                                     *
 * Author: Ferry "Wyz" Timmers                         *
 *                                                     *
 * Date: 15:29 23-8-2012                               *
 *                                                     *
 * Description: Provides a way to store and retrieve   *
 *              arbitrary sized data packages or       *
 *              streams.                               *
 *******************************************************/

#ifndef _BUFFER_H
#define _BUFFER_H

#include <queue>
#include <string>

namespace AGSSock {

//------------------------------------------------------------------------------

class Buffer : public std::queue<std::string>
{
	public:
	int error;
	
	Buffer() : error(0), std::queue<std::string>() {}
	
	void append(const std::string &data)
	{
		if (empty())
			push(data);
		else
			back().append(data);
	}
	
	void extract()
	{
		// Not checked for empty
		std::size_t pos = front().find_first_of('\0');
		if (pos == std::string::npos)
			pop();
		else
		{
			front().erase(0, pos + 1);
			// Empty strings should only be generated by the sockets API
			if (front().empty()) pop();
		}
	}
};

//------------------------------------------------------------------------------

} /* namespace AGSSock */

#endif /* _BUFFER_H */

//..............................................................................
