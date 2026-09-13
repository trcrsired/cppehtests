#include<herbceptions/error>
#include<fast_io.h>
#include<fast_io_driver/timer.h>

template<::std::size_t depth>
[[__gnu__::__noinline__]] void foo(::std::size_t val) throws
{
	if constexpr(depth<2)
	{
		if(!(val&127))
		throw throws ::std::errc::network_down;
	}
	else
	{
		foo<depth-1>(val);
	}
}


int main()
{
	fast_io::timer t(u8"herbceptions");
for(::std::size_t i{};i!=1000000;++i)	
try
{
	foo<100>(i);
}
catch throws(::std::error)
{}
}
