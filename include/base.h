#pragma once
#include <type_traits>
#include <initializer_list>
#include <malloc.h>

//-----------------------------------------------------------------------------
//	bare minimum
//-----------------------------------------------------------------------------

#undef min
#undef max

template<typename T> struct T_type { typedef T type; };
template<typename T> using type_t = typename T::type;

template<bool B, typename R = void> 		struct T_enable_if;
template<typename R> 						struct T_enable_if<true, R> : T_type<R> {};
template<bool B, typename R = void>			using enable_if_t 	= type_t<T_enable_if<B, R>>;

template<bool B, typename T, typename F>	struct T_if;
template<typename T, typename F>			struct T_if<true, T, F> 	: T_type<T> {};
template<typename T, typename F>			struct T_if<false, T, F>	: T_type<F> {};
template<bool B, typename T, typename F>	using if_t			= type_t<T_if<B, T, F>>;

template<typename T, typename R = T> 		struct T_exists : T_type<R> {};
template<typename T, typename R = T>		using exists_t		= type_t<T_exists<T, R>>;
template<typename T>						using identity_t	= type_t<T_exists<T>>;

template<typename T> constexpr bool is_pointer_v		= false;
template<typename T> constexpr bool is_pointer_v<T*>	= true;
template<typename T> constexpr bool is_integral_v		= std::is_integral<T>::value;
template<typename T> constexpr bool is_signed_v			= std::is_signed<T>::value;
template<typename T> constexpr bool is_enum_v			= std::is_enum<T>::value;
template<typename T> using underlying_type_t			= type_t<std::underlying_type<T>>;

template<typename T> struct T_noref					: T_type<T> {};
template<typename T> struct T_noref<T&>				: T_type<T> {};
template<typename T> struct T_noref<T&&>			: T_type<T> {};
template<typename T> using noref_t = type_t<T_noref<T>>;
template<typename T> constexpr noref_t<T>&	declval() noexcept;

template<typename T> struct T_deref					: T_type<noref_t<decltype(*declval<T>())>> {};
template<>			 struct T_deref<void*>			: T_type<void> {};
template<>			 struct T_deref<const void*>	: T_type<void> {};
template<typename C, typename T> struct T_deref<T C::*>			: T_type<T> {};
template<typename C, typename T> struct T_deref<const T C::*>	: T_type<const T> {};
template<typename T> using deref_t = type_t<T_deref<T>>;

template<typename T> struct remove_const : T_type<T> {};
template<typename T> struct remove_const<const T>  : T_type<T> {};
template<typename T> using remove_const_t			= type_t<remove_const<T>>;


template<typename...T> struct typelist { static const auto size = sizeof...(T); };
template<typename...T> using last_t = typename decltype((T_exists<T>{}, ...))::type;

template<typename...T>	struct T_head;
template<typename ...T>	struct T_head<typelist<T...>> : T_head<T...> {};
template<typename T0, typename ...T> struct T_head<T0, T...> : T_type<T0> { using tail = typelist<T...>; };
template<typename...T> using head_t = type_t<T_head<T...>>;
template<typename...T> using tail_t = typename T_head<T...>::tail;

template<typename L, typename...R> struct T_except_last;
template<typename...L, typename...R> struct T_except_last<typelist<L...>, typelist<R...>>			: T_except_last<typelist<L...>, R...> {};
template<typename...L, typename M, typename...R>	struct T_except_last<typelist<L...>, M, R...>	: T_except_last<typelist<L..., M>, R...> {};
template<typename...L, typename R>					struct T_except_last<typelist<L...>, R>			: T_type<typelist<L...>> {};
template<typename...T> using except_last_t = typename T_except_last<typelist<>, T...>::type;

struct _none {
	template<typename T> operator T() const { return T(); }
};
static constexpr _none none;

template<typename A, typename B> constexpr A	min(A a, B b) { return a < b ? a : b; }
template<typename A, typename B> constexpr A	max(A a, B b) { return a < b ? b : a; }
template<typename T> constexpr T				abs(T a) { return a < 0 ? -a : a; }
template<typename T> constexpr T				clamp(T t, identity_t<T> a, identity_t<T> b)	{ return min(max(t, a), b); }
template<typename T> constexpr bool				between(T x, identity_t<T> a, identity_t<T> b)	{ return a <= x && x <= b; }
template<typename A, typename B> auto 			exchange(A& a, const B& b)	{ A t = a; a = b; return t; }
template<typename T> void						swap(T& a, T& b)			{ T t = a; a = b; b = t; }
template<typename T> constexpr auto             addr(T &&t) { return &t; }

template<typename T> auto& 						unconst(const T &t)	{ return const_cast<T&>(t); }
template<typename T> auto& 						toconst(T &t)		{ return const_cast<const T&>(t); }
template<typename T> auto						unconst(const T *t)	{ return const_cast<T*>(t); }
template<typename T> auto 						toconst(T *t)		{ return const_cast<const T*>(t); }

template<typename T, int N> auto 				num_elements(T (&)[N]) { return N; }
template<typename T> constexpr auto			 	begin(T* t) 	{ return t; }
template<typename T, int N> constexpr auto	 	end(T (&t)[N]) 	{ return t + N; }

struct trivial {};
template<typename T> static constexpr bool is_trivial_v                 = std::is_trivial<T>::value || std::is_base_of<trivial, T>::value;
template<typename T> static constexpr bool is_trivially_constructible_v = std::is_trivially_default_constructible<T>::value;
template<typename T> static constexpr bool is_trivially_destructible_v  = std::is_trivially_destructible<T>::value;
template<typename T> static constexpr bool is_trivially_copyable_v      = std::is_trivially_copy_constructible<T>::value;

template<typename T> inline enable_if_t< is_trivially_copyable_v<T>> copyn(T *i, T *s, size_t n) { memcpy(i, s, n * sizeof(T)); }
template<typename D, typename S> inline void 	copyn(D i, S s, size_t n) { while (n--) *i++ = *s++; }
template<typename D, typename S> inline void 	copy(D a, D b, S s) { if (a < b) copyn(a, s, b - a); }

template<typename T>				inline bool	comparen(T *i, T *s, size_t n)	{ return memcmp(i, s, n * sizeof(T)) == 0; }
template<typename D, typename S>	inline bool	comparen(D i, S s, size_t n)	{ while (n--) if (*i++ != *s++) return false; return true; }
template<typename D, typename S>	inline bool	compare(D a, D b, S s)			{ return a >= b || comparen(a, s, b - a); }


//-----------------------------------------------------------------------------
//	integers
//-----------------------------------------------------------------------------

template<int N, bool S> using int_t = if_t<S,
	if_t<N <= 8,	int8_t, if_t<N <= 16,	int16_t, if_t<N <= 32,   int32_t, enable_if_t<N <= 64,	int64_t>>>>,
	if_t<N <= 8, uint8_t, if_t<N <= 16, uint16_t, if_t<N <= 32, uint32_t, enable_if_t<N <= 64, uint64_t>>>>
>;

typedef unsigned char byte;

//-----------------------------------------------------------------------------
//	range
//-----------------------------------------------------------------------------

template<typename T> struct range {
	using E = deref_t<T>;
	T a, b;
	constexpr range()				: a(), b() {}
	constexpr range(_none)			: range() {}
	constexpr range(T a, T b)		: a(a), b(b) {}
	constexpr range(T a, size_t n)	: a(a), b(a + n) {}
	template<size_t N> constexpr range(E (&arr)[N]) : a(arr), b(arr + N) {}
	constexpr range(std::initializer_list<E> list)	: a(list.begin()), b(list.end()) {}
	template<typename U, enable_if_t<std::is_assignable<T&,U&>::value>* = nullptr> constexpr range(const range<U> &b) : a(b.a), b(b.b) {}
	constexpr auto	empty()	            const { return a == b; }
	constexpr auto	size()	            const { return size_t(b - a); }
	constexpr T		begin()             const { return a; }
	constexpr T		end() 	            const { return b; }
	constexpr auto&	front()	            const { return *a; }
	constexpr auto&	back()	            const { return *(b - 1); }
    constexpr auto  index(T i)          const { return i < a || i >= b ? -1 : i - a; }
    constexpr auto  index(E &e)         const { return index(&e); }
	constexpr range slice(int i) 		const { return {a + i, b}; }
	constexpr range slice(int i, int j)	const { return {a + i, min(a + i + j, b)}; }
	constexpr auto	at(size_t i)		const { return a + i; }
	constexpr auto&	item(int i) 		const { return *(a + i); }
	constexpr auto&	operator[](int i) 	const { return *(a + i); }
	constexpr explicit operator bool() 	const { return !!a; }

	auto&			pop_back()		{ --b; return *this;}
	auto			find(const E &e) const {
		auto p = a;
		while (p < b && *p != e)
			++p;
		return p;
	}
	
	friend constexpr size_t num_elements(const range &t) { return t.size(); }
};

template<typename T> 				range<T>	make_range(T a, T b) 		{ return {a, b}; }
template<typename T> 				range<T>	make_range(T a, size_t n) 	{ return {a, n}; }
template<typename T, int N> 		range<T*>	make_range(T (&a)[N]) 		{ return {a, N}; }

template<typename T, bool = is_trivial_v<T>> struct alloc_block : range<T*> {
	using range<T*>::a;
	using range<T*>::b;

	alloc_block()	{}
	alloc_block(size_t n) : range<T*>((T*)malloc(n * sizeof(T)), n) {}
	alloc_block(T *a, size_t n)		: range<T*>(a, n) {}
	alloc_block(T *a, T *b)			: range<T*>(a, b) {}
	alloc_block(alloc_block &&r)	: alloc_block(r.detach(), r.b) {}
	auto &operator=(alloc_block &&r)	{ swap(a, r.a); swap(b, r.b); return *this; }

	~alloc_block()  { free(a); }
	T *detach()     { return exchange(a, nullptr); }

	auto& resize(size_t n) {
        a = (T*)realloc(a, n * sizeof(T));
        b = a + n;
		return *this;
	}
};

template<typename T> struct alloc_block<T, false> : range<T*> {
	using range<T*>::a;
	using range<T*>::b;

	alloc_block()	{}
	alloc_block(size_t n) : range<T*>(new T[n], n) {}
	alloc_block(T *a, size_t n)		: range<T*>(a, n) {}
	alloc_block(T *a, T *b)			: range<T*>(a, b) {}
	alloc_block(alloc_block &&r)	: alloc_block(r.detach(), r.b) {}
	auto &operator=(alloc_block &&r)	{ swap(a, r.a); swap(b, r.b); return *this; }

	~alloc_block()  { delete[] a; }
	T *detach()     { return exchange(a, nullptr); }

	auto& resize(size_t n) {
		auto old = a;
        a = new T[n];
		copyn(a, old, min(n, b - old));
		delete[] old;
        b = a + n;
		return *this;
	}
};

template<typename T> struct growing_block : alloc_block<T> {
	using alloc_block<T>::a;
	using alloc_block<T>::b;
	T	*p = nullptr;

	growing_block()	{}
	growing_block(size_t n)			: alloc_block<T>(n) { p = a; }
	growing_block(alloc_block<T> &&r)	: alloc_block<T>(std::move(r)) { p = a; }

	T* ensure(size_t n) {
		if (p + n >= b) {
			auto offset = p - a;
			this->resize(max(offset + n, (b - a) * 2));
			p = a + offset;
		}
		return p;
	}

	auto alloc(size_t n) {
		auto p0 = ensure(n);
		p += n;
		return p0;
	}

	void giveback(size_t size) {
		p -= size;
	}
	void finalize() {
		b = p;
	}
	size_t tell() const {
		return p - a;
	}
};

template<typename T> struct save {
	T	&t, t0;
	save(T &t, T t1) : t(t), t0(t) { t = t1;}
	~save() { t = t0; }
};

template<typename T> struct ref_helper {
    T   t;
    ref_helper(T t) : t(std::move(t)) {}
    auto operator->() { return &t; }
    auto operator->() const { return &t; }
};
