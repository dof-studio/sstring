// sstring.hpp
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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <memory>
#include <memory_resource>
#include <cassert>
#include <stdalign.h>

// must support C++ 20
#if defined(_MSVC_LANG)
// MSVC Special Case
#if _MSVC_LANG < 202002L
#error "C++20 is required for project sstring!"
#endif
#elif defined(__cplusplus)
#if __cplusplus < 202002L
#error "C++20 is required for project sstring!"
#endif
#else
#error "C++ is required to compile project sstring!"
#endif

// define sstring virtual destructor
#ifndef _SSTRING_IS_VIRTUAL_DESTRUCTOR
#define _SSTRING_IS_VIRTUAL_DESTRUCTOR     0
#endif

// namespace libsstring starts
namespace libsstring {

    // Empty Allocator
    template<typename Alloc>
    struct allocator_holder_empty : private Alloc {
        constexpr allocator_holder_empty() = default;
        constexpr allocator_holder_empty(const Alloc& a) : Alloc(a) {}
        constexpr Alloc& get_allocator() noexcept { return *this; }
        constexpr const Alloc& get_allocator() const noexcept { return *this; }
    };

    // Non-empty Allocator
    template<typename Alloc>
    struct allocator_holder_nonempty {
        Alloc a;
        constexpr allocator_holder_nonempty() = default;
        constexpr allocator_holder_nonempty(const Alloc& alloc) : a(alloc) {}
        constexpr Alloc& get_allocator() noexcept { return a; }
        constexpr const Alloc& get_allocator() const noexcept { return a; }
    };

    // Allocator Holder
    template<typename Alloc>
    using allocator_holder = std::conditional_t<
        std::is_empty_v<Alloc>&& std::is_default_constructible_v<Alloc>,
        allocator_holder_empty<Alloc>,
        allocator_holder_nonempty<Alloc>
    >;

    // Implementation of sstring
    template<
        typename CharT = char,
        typename Traits = std::char_traits<CharT>,
        typename Allocator = std::allocator<CharT>,
        typename SSO_FlagType = std::uint8_t,
        size_t   SSO_ReservedBytes = 30,
        size_t   SSO_StructAlignByte = 8
    >
    class basic_sstring : private allocator_holder<Allocator> {
        static_assert(sizeof(CharT) == 1, "basic_sstring currently supports only byte-sized CharT, aka. char");
    // Private types
    private:
        using allocator_type = Allocator;
        using traits_type = Traits;
        using alloc_traits = std::allocator_traits<allocator_type>;

    // Public types
    public:
        using value_type = CharT;
        using traits_type_public = Traits;
        using allocator_type_public = Allocator;
        using flag_type = SSO_FlagType;
        using byte_type = unsigned char;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = value_type*;
        using const_iterator = const value_type*;
        using reverse_iterator = value_type*;
        using const_reverse_iterator = const value_type*;

        static constexpr size_type npos = static_cast<size_type>(-1);

    private:
        // Storage union aims to keep object compact. We separate allocator (via base or member).
        union Storage {
            // small string optimization
            struct alignas(SSO_StructAlignByte) SSO {
                byte_type buf[SSO_ReservedBytes];
                flag_type len;         // length (0 -> N - 1)
                flag_type tag;         // tag byte (used to overlap heap.cap MSB)
            } sso;

            // heap allocated strings, compatible for std::string
            struct alignas(SSO_StructAlignByte) Heap {
                CharT* ptr;            // data ptr
                size_type size;        // size, used
                size_type cap;         // capacity, 
                size_type flag;        // highest bit used as is - heap flag
            } heap;

            // default constructor
            constexpr Storage() noexcept { std::memset(this, 0, sizeof(Storage)); }
        } storage;

        // total length of a storage
        static constexpr size_type TOTAL_BYTES = sizeof(Storage);
        
        // cap flag uses highest bit of size_t
        static constexpr size_type SIZE_T_BITS = sizeof(size_type) * 8;
        static constexpr size_type HEAP_FLAG = size_type(1) << (SIZE_T_BITS - 1);

        // requires little-endian for tag-cap overlap strategy
        static constexpr bool platform_is_little_endian() {
            #if defined(__cpp_lib_endian)
            return std::endian::native == std::endian::little;
            #else
            # if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
            return true;
            # else
            return true; // assume little-endian on typical targets
            # endif
            #endif
        }

        // @brief reset storage to nothing - sso with 0 length
        static constexpr void reset_storage(Storage& dest) {
            std::memset(&dest, 0, sizeof(Storage));
        }

        // @brief copy storage
        static constexpr void copy_storage(Storage& dest, const Storage& src) {
            std::memcpy(&dest, &src, sizeof(Storage));
        }

        // @brief steal storage
        static constexpr void steal_storage(Storage& dest, Storage& src) {
            copy_storage(dest, src);
            reset_storage(src);
        }

        // @brief get is heap allocated
        constexpr bool is_heap() const noexcept {
            return (storage.heap.flag & HEAP_FLAG) != 0;
        }
        
        // @brief get is sso mode
        constexpr bool is_sso() const noexcept {
            return !is_heap();
        }

        // @brief set flag as heap-allocated
        constexpr void set_heap_flag() noexcept {
            storage.heap.flag |= HEAP_FLAG;
        }
        
        // @brief set as non-heap allocated, aka. sso
        constexpr void clear_heap_flag() noexcept {
            storage.sso.tag = 0;
        }
        
        // @brief get raw heap capacity as a size_type
        constexpr size_type heap_capacity_raw() const noexcept {
            return storage.heap.cap;
        }
       
        // @brief get raw sso capacity as a size_type
        static constexpr size_type sso_capacity_bytes() noexcept { 
            // N bytes
            return sizeof(storage.sso.buf); 
        } 
        
        // @brief get efficient raw sso capacity as a size_type
        static constexpr size_type sso_max_size() noexcept { 
            // N - 1 chars (with '\0')
            return sso_capacity_bytes() - 1; 
        }      

        // @brief comparasion simd memchr
        static constexpr const void* simd_memchr(const void* buf, int c, size_t n) noexcept {
            return std::memchr(buf, c, n);
        }

        // @brief comparasion simd memcmp
        static constexpr int simd_memcmp(const void* a, const void* b, size_t n) noexcept {
            return std::memcmp(a, b, n);
        }

        // @brief get allocator access
        constexpr allocator_type& get_alloc() noexcept {
            return allocator_holder<Allocator>::get_allocator();
        }
        constexpr const allocator_type& get_alloc() const noexcept {
            return allocator_holder<Allocator>::get_allocator(); 
        }

        // @brief ensure null-termination depending on mode
        constexpr void ensure_sso_null_terminated() noexcept {
            storage.sso.buf[storage.sso.len] = '\0'; 
        }
        constexpr void ensure_heap_null_terminated() noexcept {
            storage.heap.ptr[storage.heap.size] = '\0'; 
        }

        // @brief allocate buffer via allocator_traits
        constexpr CharT* allocate_buffer(size_type capacity) {
            // allocate capacity elements (capacity includes space for null terminator)
            return alloc_traits::allocate(get_alloc(), capacity);
        }
        constexpr void deallocate_buffer(CharT* p, size_type cap) noexcept {
            alloc_traits::deallocate(get_alloc(), p, cap);
        }

        // @brief copy using traits
        constexpr static void traits_copy(CharT* dest, const CharT* src, size_type count) {
            // Use move for potential overlap; here we call move if implementations provide it.
            Traits::move(dest, src, count);
        }

        // @brief move using traits
        constexpr static void traits_move(CharT* dest, const CharT* src, size_type count) {
            Traits::move(dest, src, count);
        }

        // @brief ensure heap mode and reserve at least new_capacity bytes (includes space for null termin.)
        constexpr void make_non_sso_and_reserve(size_type new_capacity) {
            // if sso, then need to copy data
            if (is_sso()) {
                size_type cur_len = storage.sso.len;
                size_type cap = std::max(new_capacity, cur_len + 1);
                CharT* p = allocate_buffer(cap);
                // copy from sso.buf to p; sso.buf is bytes; reinterpret as CharT*
                std::memcpy(p, storage.sso.buf, cur_len); // safe because CharT is 1 byte
                p[cur_len] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = cur_len;
                storage.heap.cap = cap;
                set_heap_flag();
            }
            else {
                size_type cur_cap = heap_capacity_raw();
                if (cur_cap >= new_capacity) [[unlikely]] {
                    return;
                }
                size_type newcap = std::max(new_capacity, cur_cap * 2);
                CharT* p = allocate_buffer(newcap);
                // copy existing
                std::memcpy(p, storage.heap.ptr, storage.heap.size);
                p[storage.heap.size] = '\0';
                // dealloc old
                deallocate_buffer(storage.heap.ptr, cur_cap);
                storage.heap.ptr = p;
                storage.heap.cap = newcap;
                set_heap_flag();
            }
        }

    public:
        // check little endian support - we currently only support little endian devices
        static_assert(platform_is_little_endian(), "basic_sstring currently assumes little-endian layout for SSO tag trick.");

        // @brief construct a basic_sstring from an empty allocator
        constexpr basic_sstring() noexcept(std::is_nothrow_default_constructible_v<allocator_type>) {
            // empty SSO
            storage.sso.len = 0;
            storage.sso.tag = 0;
            storage.sso.buf[0] = '\0';
        }

        // @brief construct a basic_sstring from an allocator
        constexpr explicit basic_sstring(const allocator_type& alloc) noexcept : allocator_holder<Allocator>(alloc) {
            // empty SSO
            storage.sso.len = 0;
            storage.sso.tag = 0;
            storage.sso.buf[0] = '\0';
        }

        // @brief construct a basic_sstring using copy construction
        constexpr basic_sstring(const basic_sstring& other)
            : allocator_holder<Allocator>(alloc_traits::select_on_container_copy_construction(other.get_alloc()))
        {
            // SSO, just copy the storage
            if (other.is_sso()) [[likely]] {
                std::memcpy(&storage, &other.storage, sizeof(Storage));
            }
            // Heap, do allocation
            else {
                size_type sz = other.storage.heap.size;
                size_type cap = other.heap_capacity_raw();
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, other.storage.heap.ptr, sz);
                p[sz] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = sz;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @brief construct a basic_sstring using copy construction and an allocator
        constexpr basic_sstring(const basic_sstring& other, const allocator_type& alloc)
            : allocator_holder<Allocator>(alloc)
        {
            // SSO, just copy the storage
            if (other.is_sso()) [[likely]] {
                copy_storage(storage, other.storage);
            }
            // Heap, do allocation
            else {
                size_type sz = other.storage.heap.size;
                size_type cap = other.heap_capacity_raw();
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, other.storage.heap.ptr, sz);
                p[sz] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = sz;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @brief construct a basic_sstring using move semantics
        constexpr basic_sstring(basic_sstring&& other) noexcept(
            std::is_nothrow_move_constructible_v<allocator_type>
            ) : allocator_holder<Allocator>(std::move(other.get_alloc()))
        {
            // Regardless of sso or not, steal the ownership
            steal_storage(storage, other.storage);
        }

        // @brief construct a basic_sstring using move semantics and an allocator
        constexpr basic_sstring(basic_sstring&& other, const allocator_type& alloc) : allocator_holder<Allocator>(alloc) {
            // If alloc == other.get_alloc() then we can steal pointer; else we copy
            if (other.is_sso()) [[likely]] {
                steal_storage(storage, other.storage);
            }
            // Heap, do move semantics if possible
            else {
                if (alloc_traits::propagate_on_container_move_assignment::value &&
                    alloc == other.get_alloc()) {
                    // steal
                    steal_storage(storage, other.storage);
                }
                else {
                    // allocate and copy
                    size_type sz = other.storage.heap.size;
                    size_type cap = other.heap_capacity_raw();
                    CharT* p = allocate_buffer(cap);
                    std::memcpy(p, other.storage.heap.ptr, sz);
                    p[sz] = '\0';
                    storage.heap.ptr = p;
                    storage.heap.size = sz;
                    storage.heap.cap = cap;
                    set_heap_flag();
                }
            }
        }

        // @brief construct from a C-string
        constexpr basic_sstring(const CharT* s) : allocator_holder<Allocator>() {
            // nullptr
            if (!s) {
                reset_storage(storage);
                return;
            }
            // SSO, just copy the storage
            size_type len = Traits::length(s);
            if (len <= sso_max_size()) [[likely]] {
                std::memcpy(storage.sso.buf, s, len);
                storage.sso.len = static_cast<flag_type>(len);
                storage.sso.tag = 0;
                storage.sso.buf[len] = '\0';
            }
            // Copy semantics
            else {
                size_type cap = len + 1;
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, s, len);
                p[len] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = len;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @brief construct from a C-string and an allocator
        constexpr basic_sstring(const CharT* s, const allocator_type& alloc) : allocator_holder<Allocator>(alloc) {
            // nullptr
            if (!s) {
                reset_storage(storage);
                return;
            }

            // SSO, just copy the storage
            size_type len = Traits::length(s);
            if (len <= sso_max_size()) [[likely]] {
                std::memcpy(storage.sso.buf, s, len);
                storage.sso.len = static_cast<flag_type>(len);
                storage.sso.buf[len] = '\0';
                storage.sso.tag = 0;
            }
            // dynamically
            else {
                size_type cap = len + 1;
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, s, len);
                p[len] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = len;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @brief construct from a string_view
        constexpr basic_sstring(std::basic_string_view<CharT, Traits> sv) : allocator_holder<Allocator>() {
            // SSO, just copy the storage
            size_type len = sv.size();
            if (len <= sso_max_size()) [[likely]] {
                std::memcpy(storage.sso.buf, sv.data(), len);
                storage.sso.len = static_cast<flag_type>(len);
                storage.sso.tag = 0;
                storage.sso.buf[len] = '\0';
            }
            // dynamically
            else {
                size_type cap = len + 1;
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, sv.data(), len);
                p[len] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = len;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @brief construct from a string_view and an allocator
        constexpr basic_sstring(std::basic_string_view<CharT, Traits> sv, const allocator_type& alloc) : allocator_holder<Allocator>(alloc) {
            // SSO, just copy the storage
            size_type len = sv.size();
            if (len <= sso_max_size()) [[likely]] {
                std::memcpy(storage.sso.buf, sv.data(), len);
                storage.sso.len = static_cast<flag_type>(len);
                storage.sso.buf[len] = '\0';
                storage.sso.tag = 0;
            }
            // dynamically
            else {
                size_type cap = len + 1;
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, sv.data(), len);
                p[len] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = len;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @brief basic_sstring fill constructor, filled with a CharT
        constexpr basic_sstring(size_type count, CharT ch, const allocator_type& alloc = allocator_type())
            : allocator_holder<Allocator>(alloc)
        {
            // SSO, just copy the storage
            if (count <= sso_max_size()) [[likely]] {
                std::memset(storage.sso.buf, static_cast<unsigned char>(ch), count);
                storage.sso.len = static_cast<flag_type>(count);
                storage.sso.buf[count] = '\0';
                storage.sso.tag = 0;
            }
            // dynamically
            else {
                size_type cap = count + 1;
                CharT* p = allocate_buffer(cap);
                std::memset(p, static_cast<unsigned char>(ch), count);
                p[count] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = count;
                storage.heap.cap = cap;
                set_heap_flag();
            }
        }

        // @TODO
        // needs more constructors

        // @brief destructor
        #if _SSTRING_IS_VIRTUAL_DESTRUCTOR != 0
        virtual ~basic_sstring() {
        #else
        ~basic_sstring() {
        #endif
            // If not heap, dealloate res
            if (is_heap()) [[unlikely]] {
                size_type cap = heap_capacity_raw();
                deallocate_buffer(storage.heap.ptr, cap);
            }
        }

    public:
        // @brief basic_sstring copy assignment
        constexpr basic_sstring& operator=(const basic_sstring& rhs) {
            if (this == &rhs) {
                return *this;
            }
            // allocator propagation? we do not change allocator on copy assignment
            if (is_heap()) {
                size_type cap = heap_capacity_raw();
                deallocate_buffer(storage.heap.ptr, cap);
            }
            // SSO, just memcpy
            if (rhs.is_sso()) {
                copy_storage(storage, rhs.storage);
            }
            // dynamically
            else {
                size_type sz = rhs.storage.heap.size;
                size_type cap = rhs.heap_capacity_raw();
                CharT* p = allocate_buffer(cap);
                std::memcpy(p, rhs.storage.heap.ptr, sz);
                p[sz] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = sz;
                storage.heap.cap = cap;
                set_heap_flag();
            }
            
            return *this;
        }

        // @brief basic_sstring move assignment
        constexpr basic_sstring& operator=(basic_sstring&& rhs) noexcept(
            std::is_nothrow_move_assignable_v<allocator_type>
            ) {
            if (this == &rhs) {
                return *this;
            }
            // deallocate our buffer
            if (is_heap()) {
                size_type cap = heap_capacity_raw();
                deallocate_buffer(storage.heap.ptr, cap);
            }
            // move allocator if propagate_on_container_move_assignment
            if (alloc_traits::propagate_on_container_move_assignment::value) {
                get_alloc() = std::move(rhs.get_alloc());
            }
            // SSO, just memcpy
            if (rhs.is_sso()) {
                steal_storage(storage, rhs.storage);
            }
            // move semantics if possible
            else {
                // if allocs are equal and propagate flag, steal pointer
                if constexpr (alloc_traits::propagate_on_container_move_assignment::value) {
                    steal_storage(storage, rhs.storage);
                }
                else {
                    // allocate and copy
                    size_type sz = rhs.storage.heap.size;
                    size_type cap = rhs.heap_capacity_raw();
                    CharT* p = allocate_buffer(cap);
                    std::memcpy(p, rhs.storage.heap.ptr, sz);
                    p[sz] = '\0';
                    storage.heap.ptr = p;
                    storage.heap.size = sz;
                    storage.heap.cap = cap;
                    set_heap_flag();
                }
            }
            
            return *this;
        }

    public:
        // @brief basic query is short string
        constexpr bool is_short() const noexcept {
            return is_sso();
        }

        // @brief basic query size used
        constexpr size_type size() const noexcept {
            return is_heap() ? storage.heap.size : storage.sso.len; 
        }

        // @brief basic query length used
        constexpr size_type length() const noexcept {
            return size(); // same
        }

        // @brief basic query is empty
        constexpr bool empty() const noexcept {
            return size() == 0; 
        }

        // @brief basic query capacity allocated
        constexpr size_type capacity() const noexcept {
            return is_heap() ? heap_capacity_raw() - 1 : sso_max_size();
        }

    public:
        // @brief underlying data pointer
        constexpr const CharT* data() const noexcept {
            return is_heap() ? storage.heap.ptr : reinterpret_cast<const CharT*>(storage.sso.buf); 
        }
        constexpr CharT* data() noexcept {
            return is_heap() ? storage.heap.ptr : reinterpret_cast<CharT*>(storage.sso.buf);
        }
        
        // @brief underlying data pointer as a const C-string
        constexpr const CharT* c_str() const noexcept {
            return data(); 
        }

    public:
        // @brief random access without index checking
        constexpr reference operator[](size_type idx) noexcept {
            return data()[idx]; 
        }
        constexpr const_reference operator[](size_type idx) const noexcept {
            return data()[idx]; 
        }

        // @brief random access with index checking
        constexpr reference at(size_type idx) {
            if (idx >= size()) [[unlikely]] {
                throw std::out_of_range("basic_sstring::at");
            }
            return data()[idx];
        }
        constexpr const_reference at(size_type idx) const {
            if (idx >= size()) [[unlikely]] {
                throw std::out_of_range("basic_sstring::at");
            }
            return data()[idx];
        }

        // @brief front reference with index checking
        constexpr reference front() {
            return at(0);
        }
        constexpr const_reference front() const {
            return at(0); 
        }

        // @brief back reference with index checking
        constexpr reference back() {
            return at(size() - 1);
        }
        constexpr const_reference back() const {
            return at(size() - 1); 
        }

    public:
        // @brief basic iterators - begin iterator
        constexpr iterator begin() noexcept {
            return data(); 
        }
        constexpr const_iterator begin() const noexcept {
            return data();
        }
        constexpr const_iterator cbegin() const noexcept {
            return data();
        }

        // @brief basic iterators - end iterator
        constexpr iterator end() noexcept {
            return data() + size(); 
        }
        constexpr const_iterator end() const noexcept {
            return data() + size();
        }
        constexpr const_iterator cend() const noexcept {
            return data() + size(); 
        }

        // @brief basic iterators - reverse begin iterator
        constexpr reverse_iterator rbegin() noexcept {
            return reverse_iterator(end());
        }
        constexpr const_reverse_iterator rbegin() const noexcept {
            return const_reverse_iterator(end());
        }
        constexpr const_reverse_iterator crbegin() const noexcept {
            return const_reverse_iterator(end());
        }

        // @brief basic iterators - reverse end iterator
        constexpr reverse_iterator rend() noexcept {
            return reverse_iterator(begin());
        }
        constexpr const_reverse_iterator rend() const noexcept {
            return const_reverse_iterator(begin());
        }
        constexpr const_reverse_iterator crend() const noexcept {
            return const_reverse_iterator(begin());
        }

    public:
        // @brief clear the content
        constexpr void clear() noexcept {
            if (is_heap()) [[unlikely]] {
                storage.heap.size = 0;
                storage.heap.ptr[0] = '\0';
            }
            else {
                storage.sso.len = 0;
                storage.sso.buf[0] = '\0';
            }
        }

        // @brief reserve spaces and perhaps convert to heap allocation
        constexpr void reserve(size_type new_cap) {
            size_type need = new_cap + 1;
            if (is_sso()) [[unlikely]] {
                if (need <= sso_capacity_bytes()) {
                    return;
                }
            }
            else if (heap_capacity_raw() >= need) [[unlikely]] {
                return;
            }
            // most likely, call conversion
            make_non_sso_and_reserve(need);
        }

        // @brief reserve spaces exactly without applying growth policy
        constexpr void reserve_exact(size_type new_cap) {
            // new_cap = chars (excluding null)
            size_type need = new_cap + 1;
            if (!is_sso()) [[unlikely]] {
                if (need <= sso_capacity_bytes()) {
                    return;
                }
            }
            else {
                if (heap_capacity_raw() >= need) {
                    return;
                }
            }
            // force allocate exactly need (no doubling)
            if (is_sso()) {
                size_type cur_len = storage.sso.len;
                CharT* p = allocate_buffer(need);
                std::memcpy(p, storage.sso.buf, cur_len);
                p[cur_len] = '\0';
                storage.heap.ptr = p;
                storage.heap.size = cur_len;
                storage.heap.cap = need;
                set_heap_flag();
            }
            else {
                size_type cur_cap = heap_capacity_raw();
                if (cur_cap == need) return;
                CharT* p = allocate_buffer(need);
                std::memcpy(p, storage.heap.ptr, storage.heap.size);
                p[storage.heap.size] = '\0';
                size_type oldcap = cur_cap;
                deallocate_buffer(storage.heap.ptr, oldcap);
                storage.heap.ptr = p;
                storage.heap.cap = need;
                set_heap_flag();
            }
        }

        // @brief shrink the capacity to fit the size
        constexpr void shrink_to_fit() {
            if (is_sso()) [[unlikely]] {
                return;
            }
            size_type sz = storage.heap.size;
            if (sz <= sso_max_size()) {
                // move back to SSO
                std::memcpy(storage.sso.buf, storage.heap.ptr, sz);
                storage.sso.len = static_cast<flag_type>(sz);
                storage.sso.buf[sz] = '\0';
                size_type oldcap = heap_capacity_raw();
                deallocate_buffer(storage.heap.ptr, oldcap);
                storage.sso.tag = 0;
            }
            else {
                size_type newcap = sz + 1;
                size_type oldcap = heap_capacity_raw();
                if (newcap < oldcap) {
                    CharT* p = allocate_buffer(newcap);
                    std::memcpy(p, storage.heap.ptr, sz);
                    p[sz] = '\0';
                    deallocate_buffer(storage.heap.ptr, oldcap);
                    storage.heap.ptr = p;
                    storage.heap.cap = newcap;
                    set_heap_flag();
                }
            }
        }

        // @brief push back a character
        constexpr void push_back(CharT ch) {
            size_type cur = size();
            if (is_sso() && cur < sso_max_size()) [[likely]] {
                storage.sso.buf[cur] = static_cast<unsigned char>(ch);
                storage.sso.buf[cur + 1] = '\0';
                storage.sso.len = static_cast<flag_type>(cur + 1);
                return;
            }
            else {
                size_type need = cur + 2;
                make_non_sso_and_reserve(need);
                storage.heap.ptr[cur] = ch;
                storage.heap.ptr[cur + 1] = '\0';
                storage.heap.size = cur + 1;
                return;
            }
        }

        // @brief pop back a character
        constexpr void pop_back() {
            size_type cur = size();
            if (cur == 0) [[unlikely]] {
                return;
            }
            if (is_sso()) [[likely]] {
                storage.sso.len = static_cast<flag_type>(cur - 1);
                storage.sso.buf[cur - 1] = '\0';
            }
            else {
                storage.heap.size = cur - 1;
                storage.heap.ptr[cur - 1] = '\0';
            }
        }

        // @brief resize the container and refill it 
        constexpr void resize(size_type new_size, CharT ch = CharT()) {
            size_type cur = size();
            if (new_size == cur) {
                return;
            }
            // shrink
            if (new_size < cur) [[unlikely]] {
                if (!is_heap()) storage.sso.len = static_cast<flag_type>(new_size);
                else storage.heap.size = new_size;
                if (!is_heap()) storage.sso.buf[new_size] = '\0';
                else storage.heap.ptr[new_size] = '\0';
                return;
            }
            // enlarge
            else{
                size_type need = new_size + 1;
                make_non_sso_and_reserve(need);
                std::fill(storage.heap.ptr + cur, storage.heap.ptr + new_size, ch);
                storage.heap.size = new_size;
                storage.heap.ptr[new_size] = '\0';
            }
        }

        // @brief append a C-string to the back of current string
        constexpr basic_sstring& append(const CharT* s) {
            size_type add = Traits::length(s);
            size_type cur = size();
            size_type need = cur + add + 1;
            // sso mode and do not need to reserve
            if (is_sso() && need <= sso_capacity_bytes()) [[likely]] {
                std::memcpy(storage.sso.buf + cur, s, add);
                storage.sso.len = static_cast<flag_type>(cur + add);
                storage.sso.buf[cur + add] = '\0';
                return *this;
            }
            // non-sso mode
            else {
                make_non_sso_and_reserve(need);
                std::memcpy(storage.heap.ptr + cur, s, add);
                storage.heap.size = cur + add;
                storage.heap.ptr[storage.heap.size] = '\0';
                return *this;
            }
        }

        // @brief append a string to the back of current string
        constexpr basic_sstring& append(std::basic_string_view<CharT, Traits> sv) {
            size_type add = sv.size();
            size_type cur = size();
            size_type need = cur + add + 1;
            // sso mode and do not need to reserve
            if (is_sso() && need <= sso_capacity_bytes()) [[likely]] {
                std::memcpy(storage.sso.buf + cur, sv.data(), add);
                storage.sso.len = static_cast<flag_type>(cur + add);
                storage.sso.buf[cur + add] = '\0';
                return *this;
            }
            // non-sso mode
            else {
                make_non_sso_and_reserve(need);
                std::memcpy(storage.heap.ptr + cur, sv.data(), add);
                storage.heap.size = cur + add;
                storage.heap.ptr[storage.heap.size] = '\0';
                return *this;
            }
        }

        // @brief append a basic_sstring to the back of current string
        constexpr basic_sstring& append(const basic_sstring& other) {
            return append(std::string_view(other.data(), other.size())); 
        }
        
        // @brief operator+= to append a string to the back of current string
        constexpr basic_sstring& operator+=(std::basic_string_view<CharT, Traits> sv) {
            return append(sv); 
        }
        constexpr basic_sstring& operator+=(const basic_sstring& o) {
            return append(o);
        }
        constexpr basic_sstring& operator+=(const CharT* s) {
            return append(std::string_view(s));
        }
        constexpr basic_sstring& operator+=(CharT ch) {
            push_back(ch); return *this; 
        }

        // @brief insert a string after a specific position
        constexpr basic_sstring& insert(size_type pos, std::basic_string_view<CharT, Traits> sv) {
            // out of range
            if (pos > size()) [[unlikely]] {
                throw std::out_of_range("insert pos");
            }
            size_type add = sv.size();
            size_type cur = size();
            size_type need = cur + add + 1;
            // SSO mode and no need to reallocate
            if (is_sso() && need <= sso_capacity_bytes()) [[likely]] {
                std::memmove(storage.sso.buf + pos + add, storage.sso.buf + pos, cur - pos);
                std::memcpy(storage.sso.buf + pos, sv.data(), add);
                storage.sso.len = static_cast<flag_type>(cur + add);
                storage.sso.buf[cur + add] = '\0';
                return *this;
            }
            // dynamic heap
            else {
                make_non_sso_and_reserve(need);
                std::memmove(storage.heap.ptr + pos + add, storage.heap.ptr + pos, cur - pos);
                std::memcpy(storage.heap.ptr + pos, sv.data(), add);
                storage.heap.size = cur + add;
                storage.heap.ptr[storage.heap.size] = '\0';
                return *this;
            }
        }

        // @brief erase some length after a specific position
        constexpr basic_sstring& erase(size_type pos, size_type len = npos) {
            size_type cur = size();
            if (pos >= cur) [[unlikely]] {
                throw std::out_of_range("erase pos");
            }
            // earse all
            if (len == npos || pos + len >= cur) {
                if (is_sso()) [[likely]] {
                    storage.sso.len = static_cast<flag_type>(pos);
                    storage.sso.buf[pos] = '\0';
                }
                else {
                    storage.heap.size = pos;
                    storage.heap.ptr[pos] = '\0';
                }
                return *this;
            }
            // erase some
            size_type tail = cur - (pos + len);
            if (is_sso()) [[likely]] {
                std::memmove(storage.sso.buf + pos, storage.sso.buf + pos + len, tail);
                storage.sso.len = static_cast<flag_type>(pos + tail);
                storage.sso.buf[pos + tail] = '\0';
            }
            else {
                std::memmove(storage.heap.ptr + pos, storage.heap.ptr + pos + len, tail);
                storage.heap.size = pos + tail;
                storage.heap.ptr[storage.heap.size] = '\0';
            }
            return *this;
        }
        
        // @brief operator+ concat two strings
        constexpr basic_sstring operator+(const CharT* b) {
            basic_sstring r;
            r.reserve(size() + Traits::length(b));
            r.append(std::basic_string_view<CharT, Traits>(data(), size()));
            r.append(b);
            return r;
        }
        constexpr basic_sstring operator+(const basic_sstring& b) {
            basic_sstring r;
            r.reserve(size() + b.size());
            r.append(std::basic_string_view<CharT, Traits>(data(), size()));
            r.append(std::basic_string_view<CharT, Traits>(b.data(), b.size()));
            return r;
        }
        constexpr basic_sstring operator+(std::basic_string_view<CharT, Traits> b) {
            basic_sstring r;
            r.reserve(size() + b.size());
            r.append(std::basic_string_view<CharT, Traits>(data(), size()));
            r.append(b);
            return r;
        }
        constexpr friend basic_sstring operator+(std::basic_string_view<CharT, Traits> a, const basic_sstring& b) {
            basic_sstring r;
            r.reserve(a.size() + b.size());
            r.append(a);
            r.append(std::basic_string_view<CharT, Traits>(b.data(), b.size()));
            return r;
        }

        // @brief get a substring of curent string
        constexpr basic_sstring substr(size_type pos = 0, size_type count = npos) const {
            size_type cur = size();
            if (pos > cur) [[unlikely]] {
                throw std::out_of_range("substr pos");
            }
            if (count == npos || pos + count > cur) {
                count = cur - pos;
            }
            // reconstruct
            return basic_sstring(std::string_view(data() + pos, count), get_alloc());
        }

        // brief find a string from a position
        constexpr size_type find(std::basic_string_view<CharT, Traits> sv, size_type pos = 0) const noexcept {
            const size_type n = size();
            const size_type m = sv.size();
            if (pos > n) [[unlikely]] {
                return npos;
            }
            if (m == 0) [[unlikely]] {
                    return pos;
                }
            if (m > n - pos) [[unlikely]] {
                return npos;
            }

            const CharT* hay = data() + pos;
            const CharT* const hay_end = data() + n;
            const CharT* needle = sv.data();
            const CharT first = needle[0];

            const std::size_t remaining = n - pos;
            const std::size_t search_len = remaining - m + 1;

            const unsigned char* cur = reinterpret_cast<const unsigned char*>(hay);
            const unsigned char* end = cur + search_len;

            while (true) {
                const void* p = memchr(cur, first, end - cur);
                if (!p) {
                    return npos;
                }

                const CharT* candidate = reinterpret_cast<const CharT*>(p);
                if (Traits::compare(candidate, needle, m) == 0) {
                    return static_cast<size_type>(candidate - data());
                }

                cur = reinterpret_cast<const unsigned char*>(candidate + 1);
                if (cur >= end) {
                    return npos;
                }
            }
        }

        // @brief legacy find a string from a position
        constexpr size_type find_legacy(std::basic_string_view<CharT, Traits> sv, size_type pos = 0) const noexcept {
            // too big position
            if (pos > size()) [[unlikely]] {
                return npos;
            }
            const CharT* hay = data() + pos;
            size_type haylen = size() - pos;
            const CharT* p = std::search(hay, hay + haylen, sv.begin(), sv.end());
            if (p == hay + haylen) {
                return npos;
            }
            return static_cast<size_type>(p - data());
        }

        // @brief find a single character
        constexpr size_type find(CharT ch, size_type pos = 0) const noexcept {
            const size_type n = size();
            if (pos >= n) {
                return npos;
            }

            const unsigned char* hay = reinterpret_cast<const unsigned char*>(data() + pos);
            const unsigned char* end = reinterpret_cast<const unsigned char*>(data() + n);

            const void* p = std::memchr(hay, ch, end - hay);
            if (!p) {
                return npos;
            }
            return static_cast<size_type>(reinterpret_cast<const CharT*>(p) - data());
        }

        // @brief compare with another string
        constexpr int compare(std::basic_string_view<CharT, Traits> sv) const noexcept {
            size_type lhs_sz = size();
            size_type rhs_sz = sv.size();
            int r = std::memcmp(data(), sv.data(), std::min(lhs_sz, rhs_sz));
            if (r != 0) {
                return r;
            }
            if (lhs_sz < rhs_sz) {
                return -1; // we return -1 and 1 not original cmp
            }
            if (lhs_sz > rhs_sz) {
                return 1; // we return -1 and 1 not original cmp
            }
            return 0;
        }

        // @brief swap with another basic_sstring
        constexpr void swap(basic_sstring& other) noexcept(
            std::is_nothrow_swappable_v<allocator_type>
            ) {
            // same
            if (this == &other) [[unlikely]] {
                return;
            }
            // We'll swap allocator if propagate_on_container_swap or allocators equal
            if constexpr (alloc_traits::propagate_on_container_swap::value) {
                // swap allocator base
                using std::swap;
                swap(get_alloc(), other.get_alloc());
                // swap storage bits
                Storage tmp;
                std::memcpy(&tmp, &storage, sizeof(Storage));
                std::memcpy(&storage, &other.storage, sizeof(Storage));
                std::memcpy(&other.storage, &tmp, sizeof(Storage));
            }
            // else we must swap contents carefully
            else {
                if (get_alloc() == other.get_alloc()) {
                    Storage tmp;
                    std::memcpy(&tmp, &storage, sizeof(Storage));
                    std::memcpy(&storage, &other.storage, sizeof(Storage));
                    std::memcpy(&other.storage, &tmp, sizeof(Storage));
                }
                else {
                    // different allocators and cannot swap them: move-copy each
                    basic_sstring tmp_this(*this, get_alloc());
                    basic_sstring tmp_other(other, other.get_alloc());
                    *this = std::move(tmp_other);
                    other = std::move(tmp_this);
                }
            }
        }

        // @brief inplace trim a basic_sstring from left
        constexpr void ltrim(const CharT* chars = " \t\r\n") noexcept {
            std::basic_string_view<CharT, Traits> sv(chars);
            size_type i = 0;
            while (i < size() && sv.find(data()[i]) != std::basic_string_view<CharT>::npos) {
                ++i;
            }
            if (i > 0) {
                erase(0, i);
            }
        }

        // @brief inplace trim a basic_sstring from right
        constexpr void rtrim(const CharT* chars = " \t\r\n") noexcept {
            std::basic_string_view<CharT, Traits> sv(chars);
            size_type i = size();
            while (i > 0 && sv.find(data()[i - 1]) != std::basic_string_view<CharT>::npos) {
                --i;
            }
            if (i < size()) {
                erase(i, size() - i);
            }
        }

        // @brief inplace trim a basic_sstring from both side
        constexpr void trim(const CharT* chars = " \t\r\n") noexcept {
            ltrim(chars);
            rtrim(chars);
        }

    public:
        // @brief get the allocator
        constexpr allocator_type get_allocator() const noexcept { 
            return get_alloc(); 
        }

        // @brief convert to std::string
        constexpr std::basic_string<CharT, Traits, Allocator> to_std_string() const { 
            return std::basic_string<CharT, Traits, Allocator>(data(), size());
        }

        // @brief convert to std::string_view
        constexpr std::basic_string_view<CharT, Traits> to_std_string_view() const {
            return std::basic_string_view<CharT, Traits>(data(), size());
        }

        // @brief implicitly convert to std::basic_string_view
        constexpr operator std::basic_string_view<CharT, Traits>() const noexcept {
            return std::basic_string_view<CharT, Traits>(data(), size());
        }

        // @brief implicitly conver to std::basic_string
        constexpr operator std::basic_string<CharT, Traits, Allocator>() const {
            return std::basic_string<CharT, Traits, Allocator>(data(), size());
        }

    public:
        // @brief compare equality (in content)
        friend bool operator==(const basic_sstring& a, const basic_sstring& b) noexcept {
            return a.size() == b.size() && Traits::compare(a.data(), b.data(), a.size()) == 0;
        }
        friend bool operator!=(const basic_sstring& a, const basic_sstring& b) noexcept { 
            return !(a == b);
        }
    };

    // convenience alias for char basic sstring
    using sstring = basic_sstring<char, std::char_traits<char>, std::allocator<char>, std::uint8_t, 30, 8>;
    
    // convenience alias for char basic string with pmr
    using sstring_pmr = basic_sstring<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>, std::uint8_t, 30, 8>;

}
// namespace libsstring ends
