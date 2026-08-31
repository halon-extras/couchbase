# couchbase

This plugin adds support Couchbase databases.

## Installation

Follow the [instructions](https://docs.halon.io/manual/comp_install.html#installation) in our manual to add our package repository and then run the command below.

### Ubuntu

```
apt-get install halon-extras-couchbase
```

### RHEL

```
yum install halon-extras-couchbase
```

### Azure Linux

```
tdnf install -y halon-extras-couchbase
```

## Configuration

Configure one or more Couchbase connection profiles in the plugin configuration:

```yaml
default:
  profile: couchbase-database
  bucket: halon
profiles:
  - id: couchbase-database
    connection_string: couchbase://couchbase.example.com
    username: halon
    password: secret
```

- `id` string - Unique profile name passed to the `Couchbase` constructor
- `connection_string` string - Couchbase connection string, such as `couchbase://couchbase.example.com` or `couchbases://couchbase.example.com`
- `username` string - Couchbase username
- `password` string - Couchbase password

The plugin tries to connects all configured profiles during startup. Startup connection errors are permanent, but later errors will make functions return an error and a later retry.

## Exported functions

The constructor needs to be [imported](https://docs.halon.io/hsl/structures.html#import) from the `extras://couchbase` module path.

### Couchbase(profile, bucket [, scope [, collection]])

Creates a Couchbase collection object.

If `scope` is omitted, the bucket's default collection is used. If `scope` is provided and `collection` is omitted, the scope's default collection is used.

**Params**

- profile `string` - ID of a configured Couchbase profile
- bucket `string` - Bucket name
- scope `string` - Optional scope name
- collection `string` - Optional collection name

**Returns**

A `Couchbase` object for the selected collection. An exception is thrown if the arguments are invalid or the profile does not exist.

**Example**

```
import { Couchbase } from "extras://couchbase";

$couchbase = Couchbase("couchbase-database", "halon", "messages", "metadata");
```

### Couchbase.get(key [, options])

Gets a document.

JSON documents are decoded to their corresponding HSL value. Binary and string documents are returned as HSL strings. Documents with unrecognized Couchbase flags are returned as strings together with their numeric flags.

**Params**

- key `string` - Document key
- options `array` - Optional read options

The following options are available in the **options** array:

- `timeout` number - Operation timeout in milliseconds; must be a non-negative integer

**Returns**

On success, an associative array with a `result` property containing the document. For documents with unrecognized flags, the array also contains a numeric `flags` property.

If Couchbase reports an error, the array contains an `error` property with `code`, `category`, `error`, and `message` details. If a document marked as JSON cannot be decoded, `error` contains the JSON decoding error as a string.

**Example**

```
$document = $couchbase->get("message:123", [
    "timeout" => 1000,
]);

if (isset($document["error"]))
    throw Exception(string($document["error"]));

echo $document["result"];
```

### Couchbase.exists(key [, options])

Checks whether a document exists.

**Params**

- key `string` - Document key
- options `array` - Optional read options

The following options are available in the **options** array:

- `timeout` number - Operation timeout in milliseconds; must be a non-negative integer

**Returns**

An associative array with a boolean `result` property on success, or an `error` property with `code`, `category`, `error`, and `message` details on failure.

**Example**

```
$exists = $couchbase->exists("message:123");

if (isset($exists["error"]))
    throw Exception($exists["error"]["message"]);

if ($exists["result"])
    echo "Document exists";
```

### Couchbase.set(key, value [, options])

Creates or replaces a document.

Binary is the default format and requires `value` to be a string. With the JSON format, `value` may be any JSON-encodable HSL value and is decoded back to an HSL value by `Couchbase.get`.

**Params**

- key `string` - Document key
- value `any` - String for binary storage, or a JSON-encodable HSL value when `format` is `json`
- options `array` - Optional mutation and format options

The following options are available in the **options** array:

- `format` string - Document format: `binary` or `json`; defaults to `binary`
- `expiry` number - Document expiry in seconds; must be a non-negative integer no greater than 50 years
- `timeout` number - Operation timeout in milliseconds; must be a non-negative integer
- `durability` string - Durability level: `none`, `majority`, `majority_and_persist_to_active`, or `persist_to_majority`

**Returns**

An associative array with `result => true` on success, or an `error` property with `code`, `category`, `error`, and `message` details on failure. Invalid arguments and JSON encoding failures throw an exception.

**Example**

```
$stored = $couchbase->set("message:123", [
    "sender" => "sender@example.com",
    "attempts" => 0,
], [
    "format" => "json",
    "expiry" => 3600,
    "durability" => "majority",
]);

if (isset($stored["error"]))
    throw Exception($stored["error"]["message"]);
```

### Couchbase.delete(key [, options])

Deletes a document.

**Params**

- key `string` - Document key
- options `array` - Optional mutation options

The following options are available in the **options** array:

- `timeout` number - Operation timeout in milliseconds; must be a non-negative integer
- `durability` string - Durability level: `none`, `majority`, `majority_and_persist_to_active`, or `persist_to_majority`

**Returns**

An associative array with `result => true` on success, or an `error` property with `code`, `category`, `error`, and `message` details on failure.

**Example**

```
$deleted = $couchbase->delete("message:123", [
    "durability" => "majority",
]);

if (isset($deleted["error"]))
    throw Exception($deleted["error"]["message"]);
```

### Couchbase.increment(key [, delta [, options]])

Atomically increments a binary counter.

The `options` array may be passed as the second argument when `delta` is omitted.

**Params**

- key `string` - Counter document key
- delta `number` - Optional non-negative integer increment; defaults to `1`
- options `array` - Optional counter options

The following options are available in the **options** array:

- `initial` number - Initial non-negative integer value used if the counter does not exist
- `expiry` number - Counter expiry in seconds when created; must be a non-negative integer no greater than 50 years
- `timeout` number - Operation timeout in milliseconds; must be a non-negative integer
- `durability` string - Durability level: `none`, `majority`, `majority_and_persist_to_active`, or `persist_to_majority`

**Returns**

An associative array whose `result` property contains the new counter value on success, or an `error` property with `code`, `category`, `error`, and `message` details on failure.

**Example**

```
$counter = $couchbase->increment("delivery-attempts", 1, [
    "initial" => 0,
]);

if (isset($counter["error"]))
    throw Exception($counter["error"]["message"]);

echo $counter["result"];
```

### Couchbase.decrement(key [, delta [, options]])

Atomically decrements a binary counter without reducing it below zero.

The `options` array may be passed as the second argument when `delta` is omitted.

**Params**

- key `string` - Counter document key
- delta `number` - Optional non-negative integer decrement; defaults to `1`
- options `array` - Optional counter options

The following options are available in the **options** array:

- `initial` number - Initial non-negative integer value used if the counter does not exist
- `expiry` number - Counter expiry in seconds when created; must be a non-negative integer no greater than 50 years
- `timeout` number - Operation timeout in milliseconds; must be a non-negative integer
- `durability` string - Durability level: `none`, `majority`, `majority_and_persist_to_active`, or `persist_to_majority`

**Returns**

An associative array whose `result` property contains the new counter value on success, or an `error` property with `code`, `category`, `error`, and `message` details on failure.

**Example**

```
$counter = $couchbase->decrement("available-slots", 1, [
    "initial" => 10,
]);

if (isset($counter["error"]))
    throw Exception($counter["error"]["message"]);

echo $counter["result"];
```
