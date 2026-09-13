#include<fast_io.h>
#include<fast_io_driver/timer.h>

int main()
{
	fast_io::timer t(u8"syscall");
for(::std::size_t i{};i!=1000000;++i)	
{
	::fast_io::system_call<3,::std::ptrdiff_t>(-1); //close -1
}
}
