# sstring

Highly Optimized SSO Friendly String Implementation. Just replace your `std::string` to `libsstring::sstring`


# Usage
```C++

#include <sstring.hpp>
#include <sstring_stdext.hpp>

// ... do whatever like std::string
// for example
libsstring::sstring x = "123";
std::string_view view = x;  // okay
```
