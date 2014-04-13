#ifndef _BENCODE_H
#define _BENCODE_H

#include <stdio.h>

enum {
	BENCODE_BOOL = 1,
	BENCODE_DICT,
	BENCODE_INT,
	BENCODE_LIST,
	BENCODE_STR,
};

enum {
	BEN_OK = 0, /* No errors. Set to zero. Non-zero implies an error. */
	BEN_INVALID,      /* Invalid data was given to decoder */
	BEN_INSUFFICIENT, /* Insufficient amount of data for decoding */
	BEN_NO_MEMORY,    /* Memory allocation failed */
};

struct bencode {
	char type;
};

struct bencode_bool {
	char type;
	char b;
};

struct bencode_dict {
	char type;
	size_t n;
	size_t alloc;
	/* keys and values can be put into a same array, later */
	struct bencode **keys;
	struct bencode **values;
};

struct bencode_int {
	char type;
	long long ll;
};

struct bencode_list {
	char type;
	size_t n;
	size_t alloc;
	struct bencode **values;
};

struct bencode_str {
	char type;
	size_t len;
	char *s;
};

/*
 * Decode 'data' with 'len' bytes of data. Returns NULL on error.
 * The encoded data must be exactly 'len' bytes (not less), otherwise NULL
 * is returned. ben_decode2() function supports partial decoding ('len' is
 * larger than actual decoded message) and gives more accurate error reports.
 */
struct bencode *ben_decode(const void *data, size_t len);

/*
 * Same as ben_decode(), but allows one to set start offset for decoding with
 * 'off' and reports errors more accurately.
 *
 * '*off' must point to decoding start offset inside 'data'.
 * If decoding is successful, '*off' is updated to point to the next byte
 * after the decoded message.
 *
 * If 'error != NULL', it is updated according to the success and error of
 * the decoding. BEN_OK is success, BEN_INVALID means invalid data.
 * BEN_INSUFFICIENT means data is invalid but could be valid if more data
 * was given for decoding. BEN_NO_MEMORY means decoding ran out of memory.
 */
struct bencode *ben_decode2(const void *data, size_t len, size_t *off, int *error);

/*
 * ben_cmp() is similar to strcmp(), but compares both integers and strings.
 * An integer is always less than a string.
 *
 * ben_cmp(a, b) returns -1 if a < b, 0 if a == b, and 1 if a > b.
 */
int ben_cmp(const struct bencode *a, const struct bencode *b);

/*
 * Comparison function suitable for qsort(). Uses ben_cmp(), so this can be
 * used to order both integer and string arrays.
 */
int ben_cmp_qsort(const void *a, const void *b);

/* Get the serialization size of bencode structure 'b' */
size_t ben_encoded_size(const struct bencode *b);

/* encode 'b'. Return encoded data with a pointer, and length in '*len' */
void *ben_encode(size_t *len, const struct bencode *b);

/*
 * encode 'b' into 'data' buffer with at most 'maxlen' bytes.
 * Returns the size of encoded data.
 */
size_t ben_encode2(char *data, size_t maxlen, const struct bencode *b);

/* You must use ben_free() for all allocated bencode structures after use */
void ben_free(struct bencode *b);

/* Create a string from binary data with len bytes */
struct bencode *ben_blob(const void *data, size_t len);

/* Create a boolean from integer */
struct bencode *ben_bool(int b);

/* Create an empty dictionary */
struct bencode *ben_dict(void);

/*
 * Try to locate 'key' in dictionary. Returns the associated value, if found.
 * Returns NULL if the key does not exist.
 */
struct bencode *ben_dict_get(const struct bencode *d, const struct bencode *key);

struct bencode *ben_dict_get_by_str(const struct bencode *d, const char *key);

/*
 * Try to locate 'key' in dictionary. Returns the associated value, if found.
 * The value must be later freed with ben_free(). Returns NULL if the key
 * does not exist.
 */
struct bencode *ben_dict_pop(struct bencode *d, const struct bencode *key);

/*
 * Set 'key' in dictionary to be 'value'. An old value exists for the key
 * is freed if it exists. 'key' and 'value' are owned by the dictionary
 * after a successful call (one may not call ben_free() for 'key' or
 * 'value'). One may free 'key' and 'value' if the call is unsuccessful.
 *
 * Returns 0 on success, -1 on failure (no memory).
 */
int ben_dict_set(struct bencode *d, struct bencode *key, struct bencode *value);

/* Same as ben_dict_set(), but the key is a C string */
int ben_dict_set_by_str(struct bencode *d, const char *key, struct bencode *value);

/* Same as ben_dict_set(), but the key and value are C strings */
int ben_dict_set_str_by_str(struct bencode *d, const char *key, const char *value);

struct bencode *ben_int(long long ll);

/* Create an empty list */
struct bencode *ben_list(void);

/*
 * Append 'b' to 'list'. Returns 0 on success, -1 on failure (no memory).
 * One may not call ben_free(b) after a successful call, because the list owns
 * the object 'b'.
 */
int ben_list_append(struct bencode *list, struct bencode *b);

/*
 * Returns a Python formatted C string representation of 'b' on success,
 * NULL on failure. The returned string should be freed with free().
 *
 * Note: The string is terminated with '\0'. All instances of '\0' bytes in
 * the bencoded data are escaped so that there is only one '\0' byte
 * in the generated string at the end.
 */
char *ben_print(const struct bencode *b);

/* Create a string from C string (note bencode string may contain '\0'. */
struct bencode *ben_str(const char *s);

/* Return a human readable explanation of error returned with ben_decode2() */
const char *ben_strerror(int error);

/* ben_is_bool() returns 1 iff b is a boolean, 0 otherwise */
static inline int ben_is_bool(struct bencode *b)
{
	return b->type == BENCODE_BOOL;
}
static inline int ben_is_dict(struct bencode *b)
{
	return b->type == BENCODE_DICT;
}
static inline int ben_is_int(struct bencode *b)
{
	return b->type == BENCODE_INT;
}
static inline int ben_is_list(struct bencode *b)
{
	return b->type == BENCODE_LIST;
}
static inline int ben_is_str(struct bencode *b)
{
	return b->type == BENCODE_STR;
}

/*
 * ben_bool_const_cast(b) returns "(const struct bencode_bool *) b" if the
 * underlying object is a boolean, NULL otherwise.
 */
static inline const struct bencode_bool *ben_bool_const_cast(const struct bencode *b)
{
	return b->type == BENCODE_BOOL ? ((const struct bencode_bool *) b) : NULL;
}

/*
 * ben_bool_cast(b) returns "(struct bencode_bool *) b" if the
 * underlying object is a boolean, NULL otherwise.
 */
static inline struct bencode_bool *ben_bool_cast(struct bencode *b)
{
	return b->type == BENCODE_BOOL ? ((struct bencode_bool *) b) : NULL;
}

static inline const struct bencode_dict *ben_dict_const_cast(const struct bencode *b)
{
	return b->type == BENCODE_DICT ? ((const struct bencode_dict *) b) : NULL;
}
static inline struct bencode_dict *ben_dict_cast(struct bencode *b)
{
	return b->type == BENCODE_DICT ? ((struct bencode_dict *) b) : NULL;
}

static inline const struct bencode_int *ben_int_const_cast(const struct bencode *i)
{
	return i->type == BENCODE_INT ? ((const struct bencode_int *) i) : NULL;
}
static inline struct bencode_int *ben_int_cast(struct bencode *i)
{
	return i->type == BENCODE_INT ? ((struct bencode_int *) i) : NULL;
}

static inline const struct bencode_list *ben_list_const_cast(const struct bencode *list)
{
	return list->type == BENCODE_LIST ? ((const struct bencode_list *) list) : NULL;
}
static inline struct bencode_list *ben_list_cast(struct bencode *list)
{
	return list->type == BENCODE_LIST ? ((struct bencode_list *) list) : NULL;
}

static inline const struct bencode_str *ben_str_const_cast(const struct bencode *str)
{
	return str->type == BENCODE_STR ? ((const struct bencode_str *) str) : NULL;
}
static inline struct bencode_str *ben_str_cast(struct bencode *str)
{
	return str->type == BENCODE_STR ? ((struct bencode_str *) str) : NULL;
}

/* Return the number of keys in a dictionary 'b' */
static inline size_t ben_dict_len(const struct bencode *b)
{
	return ben_dict_const_cast(b)->n;
}

/* Return the number of items in a list 'b' */
static inline size_t ben_list_len(const struct bencode *b)
{
	return ben_list_const_cast(b)->n;
}

/*
 * ben_list_get(list, i) returns object at position i in list,
 * or NULL if position i is out of bounds.
 */
static inline struct bencode *ben_list_get(const struct bencode *list, size_t i)
{
	const struct bencode_list *l = ben_list_const_cast(list);
	return i < l->n ? l->values[i] : NULL;
}

/*
 * ben_list_set(list, i, b) sets object b to list at position i.
 * The old value at position i is freed.
 * The program aborts if position i is out of bounds.
 */
void ben_list_set(struct bencode *list, size_t i, struct bencode *b);

/* Return the number of bytes in a string 'b' */
static inline size_t ben_str_len(const struct bencode *b)
{
	return ben_str_const_cast(b)->len;
}

/* Return boolean value (0 or 1) of 'b' */
static inline int ben_bool_val(const struct bencode *b)
{
	return ben_bool_const_cast(b)->b ? 1 : 0;
}

/* Return integer value of 'b' */
static inline long long ben_int_val(const struct bencode *b)
{
	return ben_int_const_cast(b)->ll;
}

/*
 * Note: the string is always zero terminated. Also, the string may
 * contain more than one zero.
 * bencode strings are not compatible with C strings.
 */
static inline const char *ben_str_val(const struct bencode *b)
{
	return ben_str_const_cast(b)->s;
}

/*
 * ben_list_for_each() is an iterator macro for bencoded lists.
 *
 * pos is a size_t.
 */
#define ben_list_for_each(b, pos, l) \
	for ((pos) = 0; (b) = ((const struct bencode_list *) (l))->values[(pos)], (pos) < ((const struct bencode_list *) (l))->n; (pos)++)

/*
 * ben_dict_for_each() is an iterator macro for bencoded dictionaries.
 *
 * pos is a size_t.
 */
#define ben_dict_for_each(key, value, pos, d) \
	for ((pos) = 0; (key) = ((const struct bencode_dict *) (d))->keys[(pos)], (value) = ((const struct bencode_dict *) (d))->values[(pos)], (pos) < ((const struct bencode_dict *) (d))->n; (pos)++)

#endif /* _BENCODE_H */
