#include<fast_io.h>
#include<stdexcept>
#include<fast_io_driver/timer.h>

void foo() throws
{
	throw std::runtime_error("eh");
}

int main()
{
	fast_io::timer t(u8"EH");
for(::std::size_t i{};i!=1000000;++i)	
try
{
	foo();
}
catch throws(::std::error)
{}
}
