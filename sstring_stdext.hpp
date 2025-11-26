// sstring_stdext.hpp
// 
// Project sstring Version 0.0.1 built 251121
// CopyRight: 2025 Nathmath/DOF Studio
// Requires: C++20 Compiler and STL
// Website: https://github.com/dof-studio/sstring
// License: MIT License
// Copyright (c) 2016-2025 Nathmath/DOF Studio
// 
// Permission is hereby granted, free of charge, to any person 
// obtaining a copy of this software and associated documentation 
// files (the "Software"), to deal in the Software without 
// restriction, including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or sell copies 
// of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be 
// ncluded in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS 
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.

#pragma once

#include <format>
#include <ostream>
#include <istream>
#include <functional>

#include "sstring.hpp"

// namespace std starts
namespace std {

    // ostream << basic_sstring
    template<class CharT, class Traits, class Alloc>
    std::basic_ostream<CharT, Traits>&
        operator<<(std::basic_ostream<CharT, Traits>& os,
            const libsstring::basic_sstring<CharT, Traits, Alloc>& s)
    {
        return os.write(s.data(), s.size());
    }

    // istream >> basic_sstring
    template<class CharT, class Traits, class Alloc>
    std::basic_istream<CharT, Traits>&
        operator>>(std::basic_istream<CharT, Traits>& is,
            libsstring::basic_sstring<CharT, Traits, Alloc>& s)
    {
        s.clear();
        std::basic_string<CharT, Traits> tmp;
        is >> tmp;
        s.append(std::basic_string_view<CharT, Traits>(tmp));
        return is;
    }

    // getline(basic_sstring)
    template<
        class CharT, class Traits, class Alloc>
    std::istream& getline(
        std::istream& is,
        libsstring::basic_sstring<CharT, Traits, Alloc>& str,
        CharT delim = '\n'
    ) {
        str.clear();

        std::istream::sentry sentry(is, true);
        if (!sentry) return is;

        while (true) {
            int c = is.rdbuf()->sbumpc();
            if (c == Traits::eof()) {
                is.setstate(std::ios::eofbit);
                break;
            }
            if (static_cast<CharT>(c) == delim)
                break;
            str.push_back(static_cast<CharT>(c));
        }
        return is;
    }

    // formatter<basic_sstring>
    template<class CharT, class Traits, class Alloc>
    struct formatter<libsstring::basic_sstring<CharT, Traits, Alloc>, CharT>
    {
        std::formatter<std::basic_string_view<CharT, Traits>, CharT> svfmt;

        constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
            return svfmt.parse(ctx);
        }

        template<class FormatContext>
        auto format(const libsstring::basic_sstring<CharT, Traits, Alloc>& s, FormatContext& ctx) const {
            return svfmt.format(std::basic_string_view<CharT, Traits>(s.data(), s.size()), ctx);
        }
    };

    // hash specialization for basic_sstring
    template<class CharT, class Traits, class Alloc>
    struct hash<libsstring::basic_sstring<CharT, Traits, Alloc>> {
        using sstring_type = libsstring::basic_sstring<CharT, Traits, Alloc>;
        size_t operator()(const sstring_type& s) const noexcept {
            // reuse std::hash<string_view>
            return std::hash<std::basic_string_view<CharT, Traits>>{}(
                std::basic_string_view<CharT, Traits>(s.data(), s.size())
                );
        }
    };

} 
// namespace std ends
