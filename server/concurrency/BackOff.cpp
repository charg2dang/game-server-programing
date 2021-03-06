#include <Windows.h>
#include "BackOff.h"

namespace c2::concurrency
{
	void BackOff::do_backoff() 
	{
		int32_t delay = (rand() % limit);
		if (0 == delay)
		{
			return;
		}

		limit = limit + limit;
		if (limit > max_delay)
		{
			limit = max_delay;
		}

#if _M_AMD64
		spin_wait(delay);
#else
		_asm
		{
			mov ecx, delay;
		}
		custom_loop:
		_asm
		{
			loop custom_loop;
		}
#endif
	}

}