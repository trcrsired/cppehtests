#include<herbceptions/error>
#include<fast_io.h>
#include<fast_io_driver/timer.h>

int main()
{
	fast_io::timer t(u8"herbceptions");
for(::std::size_t i{};i!=1000000;++i)	
try
{
	throw throws ::std::errc::network_down;
}
catch throws(::std::error)
{}
}
