#include <node_api.h>
#include "base.h"

template<auto F, typename = decltype(F)> struct field {
	const char *name;
	inline field(const char *name) : name(name) {}
};


template<typename C, size_t N> struct fixed_string {
	C s[N];
	fixed_string() 				{ s[0] = 0; }
	fixed_string(const C *c)	{ strcpy(s, c); }
	operator const C*() const	{ return s; }
	auto& operator=(const C *c) { strcpy(s, c); return *this; }
   	auto	begin()		const	{ return s; }
};

namespace Node {

struct any_pointer {
	void *p;
	any_pointer(void *p) : p(p) {}
	template<typename T> operator T*() const { return static_cast<T*>(p); }
};

struct finalizer {
	node_api_nogc_finalize cb;
	void* hint;
	template<typename L> finalizer(L &&lambda) {
		hint	= &lambda;
		cb	    = [](napi_env, void *data, void* hint) { (*(L*)hint)(any_pointer(data)); };
	}
};

struct string_param {
	const char* utf8;
	size_t		length;
	string_param(const char* utf8, size_t length = NAPI_AUTO_LENGTH) : utf8(utf8), length(length) {}
};

//-----------------------------------------------------------------------------
//	environment
//-----------------------------------------------------------------------------

class value;
class string;
class ref;
class array;

struct environment {
	template<auto F, typename A, typename B> struct api_call;
	template<auto F, typename...A, typename B> struct api_call<F, typelist<A...>, B> {
		environment *env;
		api_call(environment *env) : env(env) {}
		auto operator()(A...a) {
			remove_const_t<deref_t<B>>	result;
			return env->checked(F(env->env, a..., &result), result);
		}
		void operator()(A...a, B result) {
			if (!env->check(F(env->env, a..., result)))
				*result = nullptr;
		}
	};
	template<auto F, typename T = decltype(F)> struct api_helper;
	template<auto F, typename...A> struct api_helper<F, napi_status(*)(A...)> : api_call<F, except_last_t<tail_t<A...>>, last_t<A...>> {
		using api_call<F, except_last_t<tail_t<A...>>, last_t<A...>>::api_call;
	};

	napi_env	env;
	environment(napi_env env) : env(env) {}
	operator napi_env() const { return env; }

	template<auto F> auto 	api() 	{ return api_helper<F>(this); }

	template<typename T> T	checked(napi_status status, T &result)	{ return check(status) ? result : T{}; }
	template<typename T> T* checked(napi_status status, T* &result) { return check(status) ? result : nullptr; }
	bool	checked(napi_status status, bool &result)				{ return check(status) && result; }

	auto	get_version() 			{ return api<napi_get_version>()(); }
	auto	get_last_error_info()	{ return api<napi_get_last_error_info>()(); }
	bool 	is_exception_pending()	{ return api<napi_is_exception_pending>()(); }

	bool 	check(napi_status status) {
		if (status != napi_ok) {
			auto info = get_last_error_info();
			printf("napi error: %s\n", info->error_message);
			return false;
		}
		return true;
	}

#if NAPI_VERSION >= 6
	bool 	set_instance_data(void* data, finalizer fin)	{ return napi_set_instance_data(env, data, fin.cb, fin.hint) == napi_ok; }
	void*	get_instance_data()								{ void *data; napi_get_instance_data(env, &data); return data; }
#endif
	napi_valuetype type(napi_value v)	{ return api<napi_typeof>()(v); }
	value 	run_script(string script);
};

static environment global_env(nullptr);

//-----------------------------------------------------------------------------
//	callbacks
//-----------------------------------------------------------------------------

struct callback {
	napi_callback	cb;
	void			*data;

	template<typename I, typename F> struct helper2;
	
	template<size_t...I, typename R, typename...A> struct helper2<std::index_sequence<I...>, R (*)(A...)> {
		template<auto F> static napi_value f(napi_env env, napi_callback_info info) {
			size_t		argc = sizeof...(I);
			napi_value	argv[sizeof...(I)];
			napi_value	this_arg;
			void*		data;
			napi_get_cb_info(env, info, &argc, argv, &this_arg, &data);
			return save(global_env.env, env), to_value(F(from_value<A>(argv[I])...));
		}
	};

	template<size_t...I, typename C, typename R, typename...A> struct helper2<std::index_sequence<I...>, R (C::*)(A...)> {
		template<auto F> static napi_value f(napi_env env, napi_callback_info info) {
			size_t		argc = sizeof...(I);
			napi_value	argv[sizeof...(I)];
			napi_value	this_arg;
			void*		data;
			napi_get_cb_info(env, info, &argc, argv, &this_arg, &data);
			auto &c = *wrapped<C>(this_arg);
			return save(global_env.env, env), to_value((c.*F)(from_value<A>(argv[I])...));
		}
	};

	template<typename I, typename C, typename...A> struct constructor_helper2;
	template<size_t...I, typename C, typename...A> struct constructor_helper2<std::index_sequence<I...>, C, A...> {
		static napi_value f(napi_env env, napi_callback_info info) {
			size_t		argc = sizeof...(I);
			napi_value	argv[sizeof...(I)];
			napi_value	this_arg;
			void*		data;
			napi_get_cb_info(env, info, &argc, argv, &this_arg, &data);
			wrapped<C>(this_arg, new C(from_value<A>(argv[I])...));
			return this_arg;
		}
	};

	template<typename F> struct helper : helper<decltype(+declval<F>)> {};
	template<typename R, typename...A>				struct helper<R (*)(A...)> 			: helper2<std::index_sequence_for<A...>, R (*)(A...)> {};
	template<typename C, typename R, typename...A>	struct helper<R (C::*)(A...)> 		: helper2<std::index_sequence_for<A...>, R (C::*)(A...)> {};
	template<typename C, typename R, typename...A>	struct helper<R (C::*)(A...) const>	: helper2<std::index_sequence_for<A...>, R (C::*)(A...)> {};

	template<auto F> static auto make() 									{ return callback(helper<decltype(F)>::template f<F>); }
	//template<auto F, typename C, typename...A> static auto make_method()	{ return helper2<std::index_sequence_for<A...>, C, A...>::template f<F>; }
	template<typename C, typename...A> static auto make_constructor()		{ return callback(constructor_helper2<std::index_sequence_for<A...>, C, A...>::f); }
	constexpr callback(napi_callback cb, void *data = nullptr) : cb(cb), data(data) {}
};


struct property_fields {
	napi_callback method	= nullptr;
	napi_callback getter	= nullptr;
	napi_callback setter	= nullptr;
};

template<typename F> struct property_maker;

template<typename C, typename T, T C::*field> napi_value getter(napi_env env, napi_callback_info info) {
	napi_value	this_arg;
	napi_get_cb_info(global_env, info, nullptr, nullptr, &this_arg, nullptr);
	return to_value(wrapped<C>(this_arg)->*field);
}

template<typename C, typename T, T C::*field> napi_value setter(napi_env env, napi_callback_info info) {
	size_t		argc = 1;
	napi_value	argv[1];
	napi_value	this_arg;
	napi_get_cb_info(global_env, info, &argc, argv, &this_arg, nullptr);
	auto ret = from_value<T>(argv[0]);
	wrapped<C>(this_arg)->*field = ret;
	return ret;
}

template<typename C, typename T> struct property_maker<T C::*> {
	template<T C::*field> static property_fields make() {
		return {nullptr, getter<C, T, field>, setter<C, T, field>};
	}
};

template<typename C, typename T> struct property_maker<const T C::*> {
	template<const T C::*field> static property_fields make() {
		return {nullptr, getter<C, T, field>, nullptr};
	}
};

template<typename C, typename R, typename...A> struct property_maker<R (C::*)(A...)> {
	template<R (C::*f)(A...)> static property_fields make() {
		return {callback::make<f>().cb, nullptr, nullptr};
	}
};

template<typename C, typename R, typename... A> struct property_maker<R (C::*)(A...) const> {
	template<R (C::*f)(A...) const> static property_fields make() {
		return {callback::make<f>().cb, nullptr, nullptr};
	}
};


struct prop_name {
	const char*	utf8name;
	napi_value 	name;
	prop_name(const char *name) : utf8name(name), name(nullptr) {}
	prop_name(napi_value name) : utf8name(nullptr), name(name) {}
};

struct property : napi_property_descriptor {
	property(prop_name name, napi_value value, napi_property_attributes attr = napi_default, void *data = nullptr)
		: napi_property_descriptor{ name.utf8name, name.name, nullptr, nullptr, nullptr, value, attr, data } {}
	property(prop_name name, napi_callback method, napi_property_attributes attr = napi_default, void *data = nullptr)
		: napi_property_descriptor{ name.utf8name, name.name, method, nullptr, nullptr, nullptr, attr, data } {}
	property(prop_name name, napi_callback getter, napi_callback setter, napi_property_attributes attr = napi_default, void *data = nullptr)
		: napi_property_descriptor{ name.utf8name, name.name, nullptr, getter, setter, nullptr, attr, data } {}
	property(prop_name name, property_fields fields, napi_property_attributes attr = napi_default, void *data = nullptr)
		: napi_property_descriptor{ name.utf8name, name.name, fields.method, fields.getter, fields.setter, nullptr, attr, data } {}

	template<auto F> property(field<F> field, napi_property_attributes attr = napi_default, void *data = nullptr)
		: property(prop_name(field.name), property_maker<decltype(F)>::template make<F>(), attr, data) {}
};

//-----------------------------------------------------------------------------
//	value
//-----------------------------------------------------------------------------

class value {
protected:
	napi_value	v;
	value()	{}
public:
	value(napi_value v) : v(v) {}
	constexpr operator napi_value() const	{ return v; }
	auto	type()					const	{ return global_env.type(v); }
	template<typename T> T is()		const	{ return T::is(v); }
	explicit operator bool()		const 	{ return !!v; }
	bool 	instanceof(value constructor) { return global_env.api<napi_instanceof>()(v, constructor); }
#if NAPI_VERSION >= 5
	ref 	add_finalizer(void* data, finalizer fin);
#endif

	friend bool	strict_equals(value lhs, value rhs) { return global_env.api<napi_strict_equals>()(lhs, rhs); }
};

struct _undefined {
	static bool 	is(napi_value v)	{ return global_env.type(v) == napi_undefined; }
	operator napi_value() const { static napi_value v(global_env.api<napi_get_undefined>()()); return v; }
} undefined;

struct _null {
	static bool 	is(napi_value v)	{ return global_env.type(v) == napi_null; }
	operator napi_value() const { static napi_value v(global_env.api<napi_get_null>()()); return v; }
} null;

//-----------------------------------------------------------------------------
//	ref
//-----------------------------------------------------------------------------

class ref {
protected:
	napi_ref	v;
public:
	ref(napi_ref v) : v(v) {}
	ref(napi_value value, uint32_t initial_refcount = 1) { global_env.api<napi_create_reference>()(value, initial_refcount, &v); }
	~ref() { napi_delete_reference(global_env, v); }

	uint32_t add_ref()			{ return global_env.api<napi_reference_ref>()(v); }
	uint32_t release()			{ return global_env.api<napi_reference_unref>()(v); }
	value 	operator*() const	{ return global_env.api<napi_get_reference_value>()(v); }
};

#if NAPI_VERSION >= 5
ref	value::add_finalizer(void* data, finalizer fin) { return global_env.api<napi_add_finalizer>()(v, data, fin.cb, fin.hint); }
#endif

template<typename T> struct refT : ref {
	refT(T v, uint32_t initial_refcount = 1) : ref(v, initial_refcount) {}
	T 	operator*() const	{ return T(global_env.api<napi_get_reference_value>()(v)); }
};


//-----------------------------------------------------------------------------
//	value types
//-----------------------------------------------------------------------------

struct boolean : value {
	explicit boolean(napi_value v) : value(v) {}
	boolean(bool value) 	{ global_env.api<napi_get_boolean>()(value, &v); }
	static boolean	coerce(value v)	{ return global_env.api<napi_coerce_to_bool>()(v); }
	operator bool()		const { return global_env.api<napi_get_value_bool>()(v); }
};

struct number : value {
	explicit number(napi_value v) : value(v) {}
	number(double value)	{ global_env.api<napi_create_double>()(value, &v); }
	number(int32_t value)	{ global_env.api<napi_create_int32>()(value, &v); }
	number(uint32_t value)	{ global_env.api<napi_create_uint32>()(value, &v); }
	number(int64_t value)	{ global_env.api<napi_create_int64>()(value, &v); }
	number(long value)			: number((int32_t)value) {}
	number(unsigned long value)	: number((uint32_t)value) {}

	static number	coerce(value v)		{ return number(global_env.api<napi_coerce_to_number>()(v)); }
	static number 	is(napi_value v)	{ return number(global_env.type(v) == napi_number ? v : nullptr); }

	operator double()	const { return global_env.api<napi_get_value_double>()(v); }
	operator int32_t()	const { return global_env.api<napi_get_value_int32>()(v); }
	operator uint32_t()	const { return global_env.api<napi_get_value_uint32>()(v); }
	operator int64_t()	const { return global_env.api<napi_get_value_int64>()(v); }
	operator long()		const { return operator int32_t(); }
	operator unsigned long()	const { return operator uint32_t(); }

	bool operator==(double b)	const { return (double)*this == b; }
	bool operator!=(double b)	const { return (double)*this != b; }
	bool operator< (double b)	const { return (double)*this <  b; }
	bool operator<=(double b)	const { return (double)*this <= b; }
	bool operator> (double b)	const { return (double)*this >  b; }
	bool operator>=(double b)	const { return (double)*this >= b; }

	auto operator+(double b)	const { return (double)*this + b; }
	auto operator-(double b)	const { return (double)*this - b; }
	auto operator*(double b)	const { return (double)*this * b; }
	auto operator/(double b)	const { return (double)*this / b; }

	auto operator&(uint32_t b)	const { return (uint32_t)*this & b; }
	auto operator|(uint32_t b)	const { return (uint32_t)*this | b; }
	auto operator^(uint32_t b)	const { return (uint32_t)*this ^ b; }

	auto operator<<(uint32_t b)	const { return (uint32_t)*this << b; }
	auto operator>>(uint32_t b)	const { return (uint32_t)*this >> b; }
};

struct string : value {
	explicit string(napi_value v) : value(v) {}
	string(const char* utf8, size_t length = NAPI_AUTO_LENGTH) 		{ global_env.api<napi_create_string_utf8>()(utf8, NAPI_AUTO_LENGTH, &v); }
	string(const char16_t* utf16, size_t length = NAPI_AUTO_LENGTH)	{ global_env.api<napi_create_string_utf16>()(utf16, length, &v); }
	//TBD: napi_create_string_latin1

	static string	coerce(value v)		{ return string(global_env.api<napi_coerce_to_string>()(v)); }
	static string 	is(napi_value v)	{ return string(global_env.type(v) == napi_string ? v : nullptr); }

	template<size_t N> operator fixed_string<char, N>()		{ fixed_string<char, N> s; global_env.api<napi_get_value_string_utf8>()(v, s.s, N); return s; }
	template<size_t N> operator fixed_string<char16_t, N>()	{ fixed_string<char16_t, N> s; global_env.api<napi_get_value_string_utf16>()(v, s.s, N); return s; }

	size_t	get_latin1(char* buf, size_t bufsize)		{ return global_env.api<napi_get_value_string_latin1>()(v, buf, bufsize); }
	size_t	get_utf8(char* buf, size_t bufsize)			{ return global_env.api<napi_get_value_string_utf8>()(v, buf, bufsize); }
	size_t	get_utf16(char16_t* buf, size_t bufsize)	{ return global_env.api<napi_get_value_string_utf16>()(v, buf, bufsize); }
	size_t	length()									{ return global_env.api<napi_get_value_string_utf16>()(v, nullptr, 0); }

#ifdef NAPI_EXPERIMENTAL
	static make_external(char* str, size_t length, node_api_nogc_finalize finalize, void* finalize_hint, napi_value* result, bool* copied) {
		napi_value	result;
		node_api_create_external_string_latin1(global_env, str, length, finalize, finalize_hint, &result, copied);
		return string(result);
	}
	static make_external(char16_t* str, size_t length, node_api_nogc_finalize finalize, void* finalize_hint, napi_value* result, bool* copied) {
		napi_value	result;
		node_api_create_external_string_utf16(global_env, str, length, finalize, finalize_hint, &result, copied);
		return string(result);
	}
#endif
};

struct symbol : value {
	explicit symbol(napi_value v) : value(v) {}
	symbol(string description) { global_env.api<napi_create_symbol>()(description, &v); }
	static symbol 	is(napi_value v)	{ return symbol(global_env.type(v) == napi_symbol ? v : nullptr); }
#if NAPI_VERSION >= 9
	static symbol symbol_for(const char* utf8description, size_t length = NAPI_AUTO_LENGTH) {
		return symbol(global_env.api<node_api_symbol_for>()(utf8description, length));
	}
#endif
};

#if NAPI_VERSION >= 5
struct Date : value {
	explicit Date(napi_value v) : value(v) {}
	Date(double time) 				{ global_env.api<napi_create_date>()(time, &v); }
	operator double() 				{ return global_env.api<napi_get_date_value>()(v); }
	static Date	is(napi_value v)	{ return Date(global_env.api<napi_is_date>()(v) ? v : nullptr); }
};
#endif

#if NAPI_VERSION >= 6
struct bigint : value {
	struct info {
		int 		sign_bit;
		size_t		word_count;
		uint64_t*	words;
	};
	explicit bigint(napi_value v) : value(v) {}
	bigint(int64_t value)				{ global_env.api<napi_create_bigint_int64>()(value, &v); }
	bigint(uint64_t value)				{ global_env.api<napi_create_bigint_uint64>()(value, &v); }
	bigint(info def) 					{ global_env.api<napi_create_bigint_words>()(def.sign_bit, def.word_count, def.words, &v); }
	bigint(bool sign_bit, size_t word_count, const uint64_t* words) { global_env.api<napi_create_bigint_words>()(sign_bit, word_count, words, &v); }
	static bigint 	is(napi_value v)	{ return bigint(global_env.type(v) == napi_bigint ? v : nullptr); }

	operator 	int64_t()				{ int64_t result; bool lossless = global_env.api<napi_get_value_bigint_int64>()(v, &result); return result; }
	operator 	uint64_t()				{ uint64_t result; bool lossless = global_env.api<napi_get_value_bigint_uint64>()(v, &result); return result; }
	info		get_info() 				{ info result; napi_get_value_bigint_words(global_env, v, &result.sign_bit, &result.word_count, result.words); return result;}
};
#endif

struct Promise : value {
	napi_deferred deferred = nullptr;
	explicit Promise(napi_value v) : value(v) {}
	Promise() 								{ global_env.api<napi_create_promise>()(&deferred, &v); }
	static bool is(napi_value v)			{ return global_env.api<napi_is_promise>()(v); }
	napi_status resolve(value resolution)	{ return napi_resolve_deferred(global_env, deferred, resolution); }	//deferred is freed
	napi_status reject(value rejection)		{ return napi_reject_deferred(global_env, deferred, rejection); }	//deferred is freed
};

//-----------------------------------------------------------------------------
//	function types
//-----------------------------------------------------------------------------

template<typename F, typename T> struct interop {
	static napi_value to_value(F x)         { return T(x); }
	static auto from_value(napi_value x)	{ return T(x); }
};

template<typename T> struct node_type;
//template<typename T> struct node_type<const T> : node_type<T> {};

template<> struct node_type<napi_value>			: interop<napi_value, napi_value> {};
template<> struct node_type<double>				: interop<double, number> {};
template<> struct node_type<int32_t>			: interop<int32_t, number> {};
template<> struct node_type<uint32_t>			: interop<uint32_t, number> {};
template<> struct node_type<int64_t>			: interop<int64_t, number> {};
template<> struct node_type<uint64_t>			: interop<uint64_t, bigint> {};
template<> struct node_type<bool>				: interop<bool, boolean> {};
template<> struct node_type<const char*>		: interop<const char*, string> {};
template<> struct node_type<const char16_t*>	: interop<const char16_t*, string> {};
template<> struct node_type<long>				: interop<long, number> {};
template<> struct node_type<unsigned long> 		: interop<unsigned long, number> {};
template<typename C, size_t N> struct node_type<fixed_string<C, N>> : interop<fixed_string<C, N>, string> {};

template<typename T> auto to_value(T x) {
	if constexpr (std::is_base_of_v<value, T>) {
		return x;
	} else {
		return node_type<T>::to_value(x);
	}
}
template<typename T> auto from_value(napi_value x)	{
	if constexpr (std::is_base_of_v<value, T>) {
		return T(x);
	} else {
		return node_type<T>::from_value(x);
	}
}

struct function : value {
	static function 	is(napi_value v)	{ return function(global_env.type(v) == napi_function ? v : nullptr); }
	template<auto& F> static function make(const char* name = nullptr, size_t length = NAPI_AUTO_LENGTH, void *data = nullptr) {
		return function(name, callback::make<F>());
	}
	explicit function(napi_value v) : value(v) {}
	function(string_param name, callback cb) { global_env.api<napi_create_function>()(name.utf8, name.length, cb.cb, cb.data, &v); }
	value 		call(std::initializer_list<napi_value> args) {
		return global_env.api<napi_call_function>()(undefined, v, args.size(), args.begin());
	}
	template<typename...A> auto operator()(A...args) {
		return call({to_value(args)...});
	}
};

//-----------------------------------------------------------------------------
//	object types
//-----------------------------------------------------------------------------

class object_base : public value {
protected:
	struct element {
		napi_value	a;
		napi_value 	i;
		element(napi_value a, napi_value i) : a(a), i(i) {}
		operator value() 				{ return global_env.api<napi_get_property>()(a, i); }
		void operator=(value v)			{ napi_set_property(global_env, a, i, v); }
		operator void*()				{ return this; }
		void operator delete(void *p)	{ auto &e = *(element*)p; global_env.api<napi_delete_property>()(e.a, e.i); }
	};

	object_base() {}
	object_base(napi_value v) : value(v) {}
public:
	typedef napi_type_tag	type_tag;

	value		getPrototype()			{ return global_env.api<napi_get_prototype>()(v); }
	array		keys();
#if NAPI_VERSION >= 6
	array 		getKeys(napi_key_collection_mode key_mode, napi_key_filter key_filter, napi_key_conversion key_conversion);
	array 		getOwnPropertyNames();
	array 		getOwnPropertySymbols();
#endif
	bool		defineProperties(range<const property*> properties) {
		return napi_define_properties(global_env, v, properties.size(), properties.begin()) == napi_ok;
	}
	element		operator[](value key)	{ return {v, key}; }
	bool		has(value key)			{ return global_env.api<napi_has_property>()(v, key); }
	bool		hasOwn(value key)		{ return global_env.api<napi_has_own_property>()(v, key); }

	value		getNamedProperty(const char *name)				{ return global_env.api<napi_get_named_property>()(v, name); }
	void		setNamedProperty(const char *name, value val)	{ napi_set_named_property(global_env, v, name, val); }

#if NAPI_VERSION >= 8
	type_tag	getTag() 				{ return global_env.api<napi_type_tag_object>()(v); }
	bool		checkTag(const type_tag* tag) { return global_env.api<napi_check_object_type_tag>()(v, tag); }
	void		freeze() 				{ napi_object_freeze(global_env, v); }
	void		seal() 					{ napi_object_seal(global_env, v); }
#endif
};

//-----------------------------------------------------------------------------
//	array
//-----------------------------------------------------------------------------

class array : public object_base {
	struct element {
		napi_value	a;
		uint32_t 	i;
		element(napi_value a, uint32_t i) : a(a), i(i) {}
		operator void*()				{ return this; }
		operator value() 				{ return global_env.api<napi_get_element>()(a, i); }
		void operator=(value v)			{ napi_set_element(global_env, a, i, v); }
		void operator delete(void *p)	{ auto &e = *(element*)p; global_env.api<napi_delete_element>()(e.a, e.i); }
	};
public:
	using object_base::operator[];
	static array	is(napi_value v)	{ return array(global_env.api<napi_is_array>()(v) ? v : nullptr); }
	explicit array(napi_value v) : object_base(v) {}
	array() 							{ global_env.api<napi_create_array>()(&v); }
	array(size_t length)				{ global_env.api<napi_create_array_with_length>()(length, &v); }

	uint32_t	length()				{ return global_env.api<napi_get_array_length>()(v); }
	uint32_t	size()					{ return length(); }
	uint32_t	fix_at(int32_t i)		{ return i < 0 ? i + length() : i; }
	element		at(int32_t i) 			{ return {v, fix_at(i)}; }
	element		operator[](uint32_t i) 	{ return {v, i}; }
	bool		has_at(int32_t i)		{ return global_env.api<napi_has_element>()(v, fix_at(i)); }
	bool		delete_at(int32_t i)	{ return global_env.api<napi_delete_element>()(v, fix_at(i)); }
	uint32_t	push(value x)			{ auto i = length(); (*this)[i] = x; return i; }
	auto 		begin();
	auto 		end();
};

inline array object_base::keys() {
	return array(global_env.api<napi_get_property_names>()(v));
}

#if NAPI_VERSION >= 6
inline array object_base::getKeys(napi_key_collection_mode key_mode, napi_key_filter key_filter, napi_key_conversion key_conversion) {
	return array(global_env.api<napi_get_all_property_names>()(v, key_mode, key_filter, key_conversion));
}
inline array object_base::getOwnPropertyNames()		{ return getKeys(napi_key_own_only, napi_key_skip_symbols, napi_key_keep_numbers); }
inline array object_base::getOwnPropertySymbols()	{ return getKeys(napi_key_own_only, napi_key_skip_strings, napi_key_keep_numbers); }
#endif

struct array_iterator {
	array		a;
	uint32_t	i;
	array_iterator(array a, uint32_t i) : a(a), i(a.fix_at(i)) {}
	auto&	operator++() 	{ ++i; return *this; }
	auto	operator*() 	{ return a[i]; }
	bool 	operator!=(array_iterator other)	{ return i != other.i; }
};
inline auto array::begin() 	{ return array_iterator(*this, 0); }
inline auto array::end() 	{ return array_iterator(*this, length()); }

//-----------------------------------------------------------------------------
//	object
//-----------------------------------------------------------------------------

class object : public object_base {
	struct element {
		napi_value	a;
		const char*	name;
		element(napi_value a, const char* name) : a(a), name(name) {}
		operator napi_value() 			{ return global_env.api<napi_get_named_property>()(a, name); }
		operator value() 				{ return global_env.api<napi_get_named_property>()(a, name); }
		void operator=(value v)			{ napi_set_named_property(global_env, a, name, v); }
	};
	struct prop : value {
		const char *name;
		prop(const char *name)	: name(name) {}
		prop(napi_value v)		: value(v), name(nullptr) {}
		value	get(napi_value obj) {
			if (name)
				global_env.api<napi_get_named_property>()(obj, name, &v);
			return *this;
		}
	};

	template<typename T, typename...X> void addNamed(const char* name, T value, X... values) {
		this->setNamedProperty(name, to_value(value));
		if constexpr (sizeof...(values) > 0)
			addNamed(values...);
	}


public:
	using object_base::operator[];
	static object 	coerce(value v)		{ return object(global_env.api<napi_coerce_to_object>()(v)); }
	static object 	is(napi_value v)	{ return object(global_env.type(v) == napi_object ? v : nullptr); }
	template<typename...X> static object   make(X... values) { auto obj = object(); obj.addNamed(values...); return obj; }

	explicit object(napi_value v) : object_base(v) {}
	object() 							{ global_env.api<napi_create_object>()(&v); }
	element		operator[](const char* name)	{ return {v, name}; }
	bool		has(const char* name)	{ return global_env.api<napi_has_named_property>()(v, name); }
	
	value 		call(prop func, std::initializer_list<napi_value> args) {
		return global_env.api<napi_call_function>()(v, func.get(v), args.size(), args.begin());
	}
	template<typename...A> value	call(prop func, A...args) {
		return call(func, {to_value(args)...});
	}

	auto		begin();
	auto		end();
};

struct object_iterator : array_iterator {
	object_base	obj;
	object_iterator(object_base obj, array keys, uint32_t i) : obj(obj), array_iterator(keys, i) {}
	auto&	operator++() 	{ ++i; return *this; }
	auto	operator*() 	{ return obj[a[i++]]; }
};
inline auto object::begin() 	{ return object_iterator(*this, keys(), 0); }
inline auto object::end() 		{ return object_iterator(*this, keys(), -1); }

struct _global {
	object	get()	const { static napi_value v(global_env.api<napi_get_null>()()); return object(v); }
	operator object()		const { return get(); }
	object	operator->()	const { return get(); }
	auto	operator[](const char *name) const { return get()[name]; }
} global;


//-----------------------------------------------------------------------------
//	errors
//-----------------------------------------------------------------------------

struct error : value {
	static error 	is(napi_value v)	{ return error(global_env.api<napi_is_error>()(v) ? v : nullptr); }
	explicit error(napi_value v) : value(v) {}
	error(string code, string msg) { global_env.api<napi_create_error>()(code, msg, &v); }
};
struct type_error : value {
	type_error(string code, string msg) { global_env.api<napi_create_type_error>()(code, msg, &v); }
};
struct range_error : value {
	range_error(string code, string msg) { global_env.api<napi_create_range_error>()(code, msg, &v); }
};

//-----------------------------------------------------------------------------
//	binary
//-----------------------------------------------------------------------------

class ArrayBuffer : public value {
public:
	static auto	is(napi_value v) 	{ return ArrayBuffer(global_env.api<napi_is_arraybuffer>()(v) ? v : nullptr); }
	explicit ArrayBuffer(napi_value v) : value(v) {}
	ArrayBuffer(size_t byte_length, void** data = nullptr) { global_env.api<napi_create_arraybuffer>()(byte_length, data, &v); }
#ifndef NODE_API_NO_EXTERNAL_BUFFERS_ALLOWED
	ArrayBuffer(void* external_data, size_t byte_length, finalizer fin) {
		global_env.api<napi_create_external_arraybuffer>()(external_data, byte_length, fin.cb, fin.hint, &v);
	}
#endif
#if NAPI_VERSION >= 7
	void	detach()			{ napi_detach_arraybuffer(global_env, v); }
	bool	is_detached()		{ return global_env.api<napi_is_detached_arraybuffer>()(v); }
#endif
	range<byte*> native() 		{ void *data; size_t size; napi_get_arraybuffer_info(global_env, v, &data, &size); return {(byte*)data, size}; }
};

struct uint8_clamped {
	uint8_t	v;
	constexpr uint8_clamped(int32_t v) : v(clamp(v, 0, 255)) {}
	constexpr operator uint8_t() const { return v; }
};

template<typename T> static constexpr auto typedarray_type = -1;
template<> static constexpr auto typedarray_type<int8_t>		= napi_int8_array;
template<> static constexpr auto typedarray_type<uint8_t>		= napi_uint8_array;
template<> static constexpr auto typedarray_type<int16_t>		= napi_int16_array;
template<> static constexpr auto typedarray_type<uint16_t>		= napi_uint16_array;
template<> static constexpr auto typedarray_type<int32_t>		= napi_int32_array;
template<> static constexpr auto typedarray_type<uint32_t>		= napi_uint32_array;
template<> static constexpr auto typedarray_type<float>			= napi_float32_array;
template<> static constexpr auto typedarray_type<double>		= napi_float64_array;
template<> static constexpr auto typedarray_type<int64_t>		= napi_bigint64_array;
template<> static constexpr auto typedarray_type<uint64_t>		= napi_biguint64_array;
template<> static constexpr auto typedarray_type<uint8_clamped>	= napi_uint8_clamped_array;

template<typename T> struct TypedArray : value {
	static TypedArray is(napi_value v) {
		if (global_env.api<napi_is_typedarray>()(v)) {
			napi_typedarray_type type;
			napi_get_typedarray_info(global_env, v, &type, nullptr, nullptr, nullptr, nullptr);
			if (type == typedarray_type<T>)
				return TypedArray(v);
		}
		return TypedArray(nullptr);
	}
	explicit TypedArray(napi_value v) : value(v) {}
	TypedArray(size_t length, ArrayBuffer arraybuffer, size_t byte_offset) {
		global_env.api<napi_create_typedarray>()(typedarray_type<T>, length, arraybuffer, byte_offset, &v);
	}
	TypedArray(size_t length, T** data = nullptr) : TypedArray(length, ArrayBuffer(length * sizeof(T), (void**)data), 0) {
	}

	range<T*> native() {
		size_t				length;
		void* 				data;
		auto status = napi_get_typedarray_info(global_env, v, nullptr, &length, &data, nullptr, nullptr);
        return status == napi_ok ? range<T*>((T*)data, length) : range<T*>();
	}
	ArrayBuffer getArrayBuffer(size_t &byte_offset) {
		napi_value	arraybuffer;
		napi_get_typedarray_info(global_env, v, nullptr, nullptr, nullptr, &arraybuffer, &byte_offset);
		return ArrayBuffer(arraybuffer);
	}
};

template<typename C> struct node_type<range<C*>> {
	static napi_value to_value(range<C*> x) {
		return TypedArray<C>(x.size(), ArrayBuffer(x.begin(), x.size(), [](void* data) {}), 0);
	}
	static auto from_value(napi_value x) {
        if (auto array = TypedArray<C>::is(x))
		    return array.native();
        return range<C*>();
	}
};

struct DataView : value {
	struct _native : range<byte*> {
		template<typename T> T		get(size_t offset) 		{ return *(T*)at(offset); }
		template<typename T> void	set(T t, size_t offset) { *(T*)at(offset) = t; }
	};
	static DataView is(napi_value v) { return DataView(global_env.api<napi_is_dataview>()(v) ? v : nullptr); }
	explicit DataView(napi_value v) : value(v) {}
	DataView(ArrayBuffer a, size_t length, size_t byte_offset)	{ global_env.api<napi_create_dataview>()(length, a, byte_offset, &v); }

	_native native() {
		size_t		byte_length;
		void*		data;
		napi_get_dataview_info(global_env, v, &byte_length, &data, nullptr, nullptr);
		return {{(byte*)data, byte_length}};
	}
	ArrayBuffer getArrayBuffer(size_t &byte_offset) {
		napi_value	arraybuffer;
		napi_get_dataview_info(global_env, v, nullptr, nullptr, &arraybuffer, &byte_offset);
		return ArrayBuffer(arraybuffer);
	}
};


//-----------------------------------------------------------------------------
//	classes
//-----------------------------------------------------------------------------

template<typename T> class external : public value {
public:
	explicit external(napi_value v)		: object(v) {}
	external(T *data, finalizer fin) { global_env.api<napi_create_external>()(data, fin.cb, fin.hint, &v); }
	template<typename...A> external(A...a) : external(new T(a...), [](T *p) { delete p; }) {}
	T*	get() 			const { return (T*)global_env.api<napi_get_value_external>()(v); }
	T&	operator*()		const { return *get(); }
	T*	operator->()	const { return get(); }
	template<typename X> decltype(auto)	operator->*(X T::*x) const { return get()->*x; }
};

template<typename T> class wrapped : public object {
	static void finalize(node_api_nogc_env env, void* data, void* hint) {
		delete static_cast<T*>(data);
	};
public:
	explicit wrapped(napi_value v)		: object(v) {}
	wrapped(T *native)					: object()	{
		napi_wrap(global_env, v, native, finalize, nullptr, nullptr);
		setNamedProperty("__proto__", Class<T>::prototype());
	}
	wrapped(napi_value v, T *native)	: object(v) {
		napi_wrap(global_env, v, native, finalize, nullptr, nullptr);
	}
	T*	get() 			const { return (T*)global_env.api<napi_unwrap>()(v); }
	T*	detach() 		const { return (T*)global_env.api<napi_remove_wrap>()(v); }
	T&	operator*()		const { return *get(); }
	T*	operator->()	const { return get(); }
	template<typename X> decltype(auto)	operator->*(X T::*x) const { return get()->*x; }
};

template<typename T, typename...A> struct ClassDefinition {
	const char				*name;
	range<const property*>	properties;
	ClassDefinition(const char	*name, std::initializer_list<property> properties) : name(name), properties(properties.begin(), properties.end()) {}
};

struct Constructor : object {
	explicit Constructor(napi_value v) : object(v) {}
	Constructor(string_param name, callback constructor, range<const property*> properties) {
		global_env.api<napi_define_class>()(name.utf8, name.length, constructor.cb, constructor.data, properties.size(), properties.begin(), &v);
	}
	template<typename T, typename...A> Constructor(const ClassDefinition<T, A...> &def) : Constructor(def.name, callback::make_constructor<T, A...>(), def.properties) {}

	napi_value	prototype()	{ return getNamedProperty("prototype");}

	object 		newInstance(std::initializer_list<napi_value> args) {
		return object(global_env.api<napi_new_instance>()(v, args.size(), args.begin()));
	}
	template<typename...A> object	newInstance(A...args) {
		return newInstance({to_value(args)...});
	}
	bool 		isInstance(value inst) {
		return global_env.api<napi_instanceof>()(inst, v);
	}
};

template<typename T> Constructor define();

template<typename T> struct Class {
	static auto		constructor()			{ static refT<Constructor> c = define<T>(); return *c; }
	static auto 	prototype()				{ return constructor().prototype(); }
	static bool 	isInstance(value inst)	{ return constructor().isInstance(inst); }
	template<typename...A> static auto newInstance(A...args) { return wrapped<T>(constructor().newInstance(args...)); }
	//template<typename...A> static auto newInstance(A...args) { return wrapped<T>(new T(args...)); }
};

//-----------------------------------------------------------------------------
//	scopes
//-----------------------------------------------------------------------------

class scope {
	napi_handle_scope	v;
public:
	scope()		{ global_env.api<napi_open_handle_scope>()(&v); }
	~scope()	{ napi_close_handle_scope(global_env, v); }
};

class escapable_scope {
	napi_escapable_handle_scope	v;
public:
	escapable_scope()	{ global_env.api<napi_open_escapable_handle_scope>()(&v); }
	~escapable_scope()	{ napi_close_escapable_handle_scope(global_env, v); }

	value escape(value escapee) {
		return global_env.api<napi_escape_handle>()(v, escapee);
	}
};

//-----------------------------------------------------------------------------
//	etc
//-----------------------------------------------------------------------------

value environment::run_script(string script)	{ return api<napi_run_script>()(script); }

#ifdef NAPI_EXPERIMENTAL
value make_string_latin1(const char* str, size_t length) { return make_value(napi_create_string_latin1, str, length); }
value make_string_utf8(const char* str, size_t length) { return make_value(napi_create_string_utf8, str, length); }
value make_string_utf16(const char16_t* str, size_t length) { return make_value(napi_create_string_utf16, str, length); }
#define NODE_API_EXPERIMENTAL_HAS_POST_FINALIZER
status node_api_post_finalizer(node_api_nogc_env env, finalize finalize_cb, void* finalize_data, void* finalize_hint);
#endif

}//namespace Node