#include <syslog.h>
#include <HalonMTA.h>
#include <couchbase/cluster.hxx>
#include <couchbase/cluster_options.hxx>
#include <couchbase/collection.hxx>
#include <couchbase/codec/raw_binary_transcoder.hxx>
#include <couchbase/codec/raw_json_transcoder.hxx>
#include <couchbase/get_options.hxx>
#include <couchbase/exists_options.hxx>
#include <couchbase/upsert_options.hxx>
#include <couchbase/remove_options.hxx>
#include <couchbase/increment_options.hxx>
#include <couchbase/decrement_options.hxx>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>

static std::string default_profile;
static std::string default_bucket;
static std::map<std::string, std::shared_ptr<couchbase::cluster>> profiles;

static bool set_exception(HalonHSLContext* hhc, const std::string& message)
{
	HalonHSLValue* c = HalonMTA_hsl_throw(hhc);
	HalonMTA_hsl_value_set(c, HALONMTA_HSL_TYPE_EXCEPTION, message.c_str(), message.size());
	return false;
}

static bool get_argument_string(HalonHSLArguments* args, size_t offset, std::string& value, bool required = true)
{
	HalonHSLValue* argument = HalonMTA_hsl_argument_get(args, offset);

	if (!required && !argument)
		return true;

	size_t al;
	char* a = nullptr;
	if (!argument || HalonMTA_hsl_value_type(argument) != HALONMTA_HSL_TYPE_STRING ||
		!HalonMTA_hsl_value_get(argument, HALONMTA_HSL_TYPE_STRING, &a, &al))
		return false;

	value = std::string(a, al);
	return true;
}

static bool get_unsigned(HalonHSLContext* hhc, HalonHSLValue* value, const char* name, std::uint64_t& result)
{
	double number;
	if (HalonMTA_hsl_value_type(value) != HALONMTA_HSL_TYPE_NUMBER || !HalonMTA_hsl_value_get(value, HALONMTA_HSL_TYPE_NUMBER, &number, nullptr) || !std::isfinite(number) || number < 0 || std::floor(number) != number || number > 9007199254740991.0)
		return set_exception(hhc, std::string(name) + " is not a non-negative integer");
	result = static_cast<std::uint64_t>(number);
	return true;
}

static void set_error(HalonHSLValue* ret, const couchbase::error& err)
{
	HalonMTA_hsl_value_set(ret, HALONMTA_HSL_TYPE_ARRAY, nullptr, 0);

	auto add_string = [&](const char* key, const std::string& value) {
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, key, 0);
		HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_STRING, value.c_str(), value.size());
	};

	auto add_number = [&](const char* key, double value) {
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, key, 0);
		HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_NUMBER, &value, 0);
	};

	add_number("code", static_cast<double>(err.ec().value()));
	add_string("category", err.ec().category().name());
	add_string("error", err.ec().message());
	add_string("message", err.message());
}

template <bool Expiry = true, typename T>
static bool set_mutation_options(HalonHSLContext* hhc, HalonHSLValue* value, T& options)
{
	if (!value)
		return true;
	if (HalonMTA_hsl_value_type(value) != HALONMTA_HSL_TYPE_ARRAY)
		return set_exception(hhc, "options is not an array");

	HalonHSLValue* option = nullptr;
	if constexpr (Expiry)
	{
		option = HalonMTA_hsl_value_array_find(value, "expiry");
		if (option)
		{
			std::uint64_t expiry;
			if (!get_unsigned(hhc, option, "expiry", expiry))
				return false;
			if (expiry > 50ULL * 365 * 24 * 60 * 60)
				return set_exception(hhc, "expiry cannot be longer than 50 years");
			options.expiry(std::chrono::seconds(expiry));
		}
	}

	option = HalonMTA_hsl_value_array_find(value, "timeout");
	if (option)
	{
		std::uint64_t timeout;
		if (!get_unsigned(hhc, option, "timeout", timeout))
			return false;
		options.timeout(std::chrono::milliseconds(timeout));
	}

	option = HalonMTA_hsl_value_array_find(value, "durability");
	if (option)
	{
		char* a = nullptr;
		size_t al;
		if (HalonMTA_hsl_value_type(option) != HALONMTA_HSL_TYPE_STRING || !HalonMTA_hsl_value_get(option, HALONMTA_HSL_TYPE_STRING, &a, &al))
			return set_exception(hhc, "durability is not a string");
		std::string durability(a, al);
		if (durability == "none")
			options.durability(couchbase::durability_level::none);
		else if (durability == "majority")
			options.durability(couchbase::durability_level::majority);
		else if (durability == "majority_and_persist_to_active")
			options.durability(couchbase::durability_level::majority_and_persist_to_active);
		else if (durability == "persist_to_majority")
			options.durability(couchbase::durability_level::persist_to_majority);
		else
			return set_exception(hhc, "invalid durability");
	}
	return true;
}

template <typename T>
static bool set_read_options(HalonHSLContext* hhc, HalonHSLValue* value, T& options)
{
	if (!value)
		return true;
	if (HalonMTA_hsl_value_type(value) != HALONMTA_HSL_TYPE_ARRAY)
		return set_exception(hhc, "options is not an array");

	HalonHSLValue* timeout_value = HalonMTA_hsl_value_array_find(value, "timeout");
	if (timeout_value)
	{
		std::uint64_t timeout;
		if (!get_unsigned(hhc, timeout_value, "timeout", timeout))
			return false;
		options.timeout(std::chrono::milliseconds(timeout));
	}
	return true;
}

template <typename T>
static bool set_counter_options(HalonHSLContext* hhc, HalonHSLValue* value, T& options)
{
	if (!set_mutation_options(hhc, value, options))
		return false;
	if (!value)
		return true;
	HalonHSLValue* initial = HalonMTA_hsl_value_array_find(value, "initial");
	if (initial)
	{
		std::uint64_t number;
		if (!get_unsigned(hhc, initial, "initial", number))
			return false;
		options.initial(number);
	}
	return true;
}

struct raw_transcoder
{
	using document_type =
		std::pair<std::vector<std::byte>, std::uint32_t>;

	static document_type decode(const couchbase::codec::encoded_value& encoded)
	{
		return { encoded.data, encoded.flags };
	}
};

template <>
struct couchbase::codec::is_transcoder<raw_transcoder>
	: public std::true_type
{
};

HALON_EXPORT
int Halon_version()
{
	return HALONMTA_PLUGIN_VERSION;
}

HALON_EXPORT
bool Halon_init(HalonInitContext* hic)
{
	HalonConfig* cfg;
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_CONFIG, nullptr, 0, &cfg, nullptr);

	HalonConfig* hcDefault = HalonMTA_config_object_get(cfg, "default");
	if (hcDefault)
	{
		const char* profile = HalonMTA_config_string_get(HalonMTA_config_object_get(hcDefault, "profile"), nullptr);
		if (profile)
			default_profile = profile;
		const char* bucket = HalonMTA_config_string_get(HalonMTA_config_object_get(hcDefault, "bucket"), nullptr);
		if (bucket)
			default_bucket = bucket;
	}

	HalonConfig* hcProfiles = HalonMTA_config_object_get(cfg, "profiles");
	if (hcProfiles)
	{
		size_t y = 0;
		HalonConfig* cfgp;
		while ((cfgp = HalonMTA_config_array_get(hcProfiles, y++)))
		{
			const char* id = HalonMTA_config_string_get(HalonMTA_config_object_get(cfgp, "id"), nullptr);
			if (!id)
			{
				syslog(LOG_CRIT, "couchbase: missing id");
				return false;
			}

			const char* connection_string = HalonMTA_config_string_get(HalonMTA_config_object_get(cfgp, "connection_string"), nullptr);
			if (!connection_string)
			{
				syslog(LOG_CRIT, "couchbase: missing connection_string");
				return false;
			}

			const char* username = HalonMTA_config_string_get(HalonMTA_config_object_get(cfgp, "username"), nullptr);
			if (!username)
			{
				syslog(LOG_CRIT, "couchbase: missing username");
				return false;
			}

			const char* password = HalonMTA_config_string_get(HalonMTA_config_object_get(cfgp, "password"), nullptr);
			if (!password)
			{
				syslog(LOG_CRIT, "couchbase: missing password");
				return false;
			}

			couchbase::cluster_options options(username, password);
			auto [err, cluster] = couchbase::cluster::connect(connection_string, options).get();
			if (err)
			{
				std::string context = err.ctx() ? err.ctx().to_json() : "{}";
				syslog(LOG_CRIT, "couchbase: connect failed: profile=%s code=%d category=%s error=%s message=%s context=%s", id, err.ec().value(), err.ec().category().name(), err.ec().message().c_str(), err.message().c_str(), context.c_str());

				auto cause = err.cause();
				while (cause)
				{
					context = cause->ctx() ? cause->ctx().to_json() : "{}";
					syslog(LOG_CRIT, "couchbase: connect failure cause: profile=%s code=%d category=%s error=%s message=%s context=%s", id, cause->ec().value(), cause->ec().category().name(), cause->ec().message().c_str(), cause->message().c_str(), context.c_str());
					cause = cause->cause();
				}
				return false;
			}

			profiles[id] = std::make_shared<couchbase::cluster>(std::move(cluster));
		}
	}
	return true;
}

HALON_EXPORT
void Halon_cleanup()
{
	for (auto& profile : profiles)
		profile.second->close().get();
}

static void Couchbase_get(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	couchbase::collection* collection = (couchbase::collection*)HalonMTA_hsl_object_ptr_get(hhc);

	std::string key;
	if (!get_argument_string(args, 0, key))
	{
		set_exception(hhc, "bad or missing key");
		return;
	}

	couchbase::get_options options;
	if (!set_read_options(hhc, HalonMTA_hsl_argument_get(args, 1), options))
		return;

	auto [err, result] = collection->get(key, options).get();
	if (err)
	{
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
		set_error(v, err);
		return;
	}

	HalonHSLValue *k, *v;
	HalonMTA_hsl_value_array_add(ret, &k, &v);
	HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "result", 0);

	auto [value, flags] = result.content_as<::raw_transcoder>();

	if (couchbase::codec::codec_flags::has_common_flags(flags, couchbase::codec::codec_flags::json_common_flags))
	{
		std::string json(reinterpret_cast<const char*>(value.data()), value.size());
		char* derr = nullptr;
		size_t derrlen;
		if (!HalonMTA_hsl_value_from_json(v, json.c_str(), &derr, &derrlen))
		{
			HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
			if (derr)
				HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_STRING, derr, derrlen);
			else
				HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_STRING, "JSON decode error", 0);
			free(derr);
		}
	}
	else if (couchbase::codec::codec_flags::has_common_flags(flags, couchbase::codec::codec_flags::string_common_flags) ||
			 couchbase::codec::codec_flags::has_common_flags(flags, couchbase::codec::codec_flags::binary_common_flags) ||
			 flags == 0)
	{
		HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_STRING, reinterpret_cast<const char*>(value.data()), value.size());
	}
	else
	{
		HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_STRING, reinterpret_cast<const char*>(value.data()), value.size());
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "flags", 0);
		double dflags = flags;
		HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_NUMBER, &dflags, 0);
	}
}

static void Couchbase_exists(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	couchbase::collection* collection = (couchbase::collection*)HalonMTA_hsl_object_ptr_get(hhc);

	std::string key;
	if (!get_argument_string(args, 0, key))
	{
		set_exception(hhc, "bad or missing key");
		return;
	}

	couchbase::exists_options options;
	if (!set_read_options(hhc, HalonMTA_hsl_argument_get(args, 1), options))
		return;

	auto [err, result] = collection->exists(key, options).get();
	if (err)
	{
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
		set_error(v, err);
		return;
	}

	bool exists = result.exists();
	HalonHSLValue *k, *v;
	HalonMTA_hsl_value_array_add(ret, &k, &v);
	HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "result", 0);
	HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_BOOLEAN, &exists, 0);
}

static void Couchbase_set(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	couchbase::collection* collection = (couchbase::collection*)HalonMTA_hsl_object_ptr_get(hhc);

	std::string key, value;
	if (!get_argument_string(args, 0, key))
	{
		set_exception(hhc, "bad or missing key");
		return;
	}

	bool json_format = false;
	auto option = HalonMTA_hsl_argument_get(args, 2);
	if (option)
	{
		if (HalonMTA_hsl_value_type(option) != HALONMTA_HSL_TYPE_ARRAY)
		{
			set_exception(hhc, "options is not an array");
			return;
		}
		HalonHSLValue* format = HalonMTA_hsl_value_array_find(option, "format");
		if (format)
		{
			size_t al;
			char* a = nullptr;
			if (!format || HalonMTA_hsl_value_type(format) != HALONMTA_HSL_TYPE_STRING ||
				!HalonMTA_hsl_value_get(format, HALONMTA_HSL_TYPE_STRING, &a, &al))
			{
				set_exception(hhc, "format is not of string type");
				return;
			}
			if (std::string(a, al) == "json")
				json_format = true;
			else if (std::string(a, al) == "binary")
				json_format = false;
			else
			{
				set_exception(hhc, "invalid format");
				return;
			}
		}
	}

	couchbase::upsert_options options;
	if (!set_mutation_options(hhc, HalonMTA_hsl_argument_get(args, 2), options))
		return;

	if (json_format)
	{
		char* json = nullptr;
		size_t jsonlen;
		if (!HalonMTA_hsl_value_to_json(HalonMTA_hsl_argument_get(args, 1), &json, &jsonlen))
		{
			set_exception(hhc, json ? json : "HalonMTA_hsl_value_to_json failed");
			free(json);
			return;
		}
		value = std::string(json, jsonlen);
		free(json);

		auto [err, result] = collection->upsert<couchbase::codec::raw_json_transcoder>(key, value, options).get();
		if (err)
		{
			HalonHSLValue *k, *v;
			HalonMTA_hsl_value_array_add(ret, &k, &v);
			HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
			set_error(v, err);
			return;
		}
	}
	else
	{
		if (!get_argument_string(args, 1, value))
		{
			set_exception(hhc, "bad or missing value");
			return;
		}
		couchbase::codec::binary data(reinterpret_cast<const std::byte*>(value.data()), reinterpret_cast<const std::byte*>(value.data() + value.size()));
		auto [err, result] = collection->upsert<couchbase::codec::raw_binary_transcoder>(key, data, options).get();
		if (err)
		{
			HalonHSLValue *k, *v;
			HalonMTA_hsl_value_array_add(ret, &k, &v);
			HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
			set_error(v, err);
			return;
		}
	}

	bool success = true;
	HalonHSLValue *k, *v;
	HalonMTA_hsl_value_array_add(ret, &k, &v);
	HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "result", 0);
	HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_BOOLEAN, &success, 0);
}

static void Couchbase_delete(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	couchbase::collection* collection = (couchbase::collection*)HalonMTA_hsl_object_ptr_get(hhc);

	std::string key;
	if (!get_argument_string(args, 0, key))
	{
		set_exception(hhc, "bad or missing key");
		return;
	}

	couchbase::remove_options options;
	if (!set_mutation_options<false>(hhc, HalonMTA_hsl_argument_get(args, 1), options))
		return;

	auto [err, result] = collection->remove(key, options).get();
	if (err)
	{
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
		set_error(v, err);
		return;
	}

	bool success = true;
	HalonHSLValue *k, *v;
	HalonMTA_hsl_value_array_add(ret, &k, &v);
	HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "result", 0);
	HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_BOOLEAN, &success, 0);
}

template <bool Increment>
static void Couchbase_counter(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	couchbase::collection* collection = (couchbase::collection*)HalonMTA_hsl_object_ptr_get(hhc);

	std::string key;
	if (!get_argument_string(args, 0, key))
	{
		set_exception(hhc, "bad or missing key");
		return;
	}

	HalonHSLValue* delta_value = HalonMTA_hsl_argument_get(args, 1);
	HalonHSLValue* options_value = HalonMTA_hsl_argument_get(args, 2);
	std::uint64_t delta = 1;
	if (delta_value && HalonMTA_hsl_value_type(delta_value) == HALONMTA_HSL_TYPE_ARRAY)
	{
		if (options_value)
		{
			set_exception(hhc, "options provided twice");
			return;
		}
		options_value = delta_value;
	}
	else if (delta_value && !get_unsigned(hhc, delta_value, "delta", delta))
		return;

	using options_type = std::conditional_t<Increment, couchbase::increment_options, couchbase::decrement_options>;
	options_type options;
	options.delta(delta);
	if (!set_counter_options(hhc, options_value, options))
		return;

	auto [err, result] = [&]() {
		if constexpr (Increment)
			return collection->binary().increment(key, options).get();
		else
			return collection->binary().decrement(key, options).get();
	}();
	if (err)
	{
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "error", 0);
		set_error(v, err);
		return;
	}

	double content = static_cast<double>(result.content());
	HalonHSLValue *k, *v;
	HalonMTA_hsl_value_array_add(ret, &k, &v);
	HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_STRING, "result", 0);
	HalonMTA_hsl_value_set(v, HALONMTA_HSL_TYPE_NUMBER, &content, 0);
}

static void collection_free(void* ptr)
{
	delete static_cast<couchbase::collection*>(ptr);
}

static void Couchbase(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	std::string profile, bucket, scope, collection;
	if (!get_argument_string(args, 0, profile))
	{
		if (default_profile.empty())
		{
			set_exception(hhc, "bad or missing profile");
			return;
		}
		profile = default_profile;
	}
	if (!get_argument_string(args, 1, bucket))
	{
		if (default_bucket.empty())
		{
			set_exception(hhc, "bad or missing bucket");
			return;
		}
		bucket = default_bucket;
	}
	if (!get_argument_string(args, 2, scope, false))
	{
		set_exception(hhc, "bad or missing scope");
		return;
	}
	if (!get_argument_string(args, 3, collection, false))
	{
		set_exception(hhc, "bad or missing collection");
		return;
	}

	auto pIter = profiles.find(profile);
	auto pPtr = pIter != profiles.end() ? pIter->second : nullptr;
	if (!pPtr)
	{
		set_exception(hhc, "unknown Couchbase profile");
		return;
	}

	couchbase::collection* objptr = nullptr;
	auto b = pPtr->bucket(bucket);
	if (scope.empty())
	{
		objptr = new couchbase::collection(b.default_collection());
	}
	else
	{
		auto s = b.scope(scope);
		if (collection.empty())
			objptr = new couchbase::collection(s.collection(s.default_name));
		else
		{
			objptr = new couchbase::collection(s.collection(collection));
		}
	}

	HalonHSLObject* object = HalonMTA_hsl_object_new();
	HalonMTA_hsl_object_type_set(object, "Couchbase");
	HalonMTA_hsl_object_register_function(object, "get", Couchbase_get);
	HalonMTA_hsl_object_register_function(object, "exists", Couchbase_exists);
	HalonMTA_hsl_object_register_function(object, "increment", Couchbase_counter<true>);
	HalonMTA_hsl_object_register_function(object, "decrement", Couchbase_counter<false>);
	HalonMTA_hsl_object_register_function(object, "set", Couchbase_set);
	HalonMTA_hsl_object_register_function(object, "delete", Couchbase_delete);
	HalonMTA_hsl_object_ptr_set(object, objptr, collection_free);
	HalonMTA_hsl_value_set(ret, HALONMTA_HSL_TYPE_OBJECT, object, 0);
	HalonMTA_hsl_object_delete(object);
}

HALON_EXPORT
bool Halon_hsl_register(HalonHSLRegisterContext* hhrc)
{
	HalonMTA_hsl_module_register_function(hhrc, "Couchbase", Couchbase);
	return true;
}