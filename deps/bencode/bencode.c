#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include "bencode.h"

#define MAX_ALLOC (((size_t) -1) / sizeof(struct bencode *) / 2)

struct decode {
	const char *data;
	const size_t len;
	size_t off;
	int error;
	int level;
};

/*
 * Buffer size for fitting all unsigned long long and long long integers,
 * assuming it is at most 64 bits. If long long is larger than 64 bits,
 * an error is produced when too large an integer is converted.
 */
#define LONGLONGSIZE 21

struct bencode_keyvalue {
	struct bencode *key;
	struct bencode *value;
};

static struct bencode *decode(struct decode *ctx);

static size_t find(const char *data, size_t len, size_t off, char c)
{
	for (; off < len; off++) {
		if (data[off] == c)
			return off;
	}
	return -1;
}

static size_t type_size(int type)
{
	switch (type) {
	case BENCODE_BOOL:
		return sizeof(struct bencode_bool);
	case BENCODE_DICT:
		return sizeof(struct bencode_dict);
	case BENCODE_INT:
		return sizeof(struct bencode_int);
	case BENCODE_LIST:
		return sizeof(struct bencode_list);
	case BENCODE_STR:
		return sizeof(struct bencode_str);
	default:
		fprintf(stderr, "Unknown bencode type: %d\n", type);
		abort();
	}
}

static void *alloc(int type)
{
	struct bencode *b = calloc(1, type_size(type));
	if (b == NULL)
		return NULL;
	b->type = type;
	return b;
}

static int insufficient(struct decode *ctx)
{
	ctx->error = BEN_INSUFFICIENT;
	return -1;
}

static int invalid(struct decode *ctx)
{
	ctx->error = BEN_INVALID;
	return -1;
}

static void *insufficient_ptr(struct decode *ctx)
{
	ctx->error = BEN_INSUFFICIENT;
	return NULL;
}

static void *invalid_ptr(struct decode *ctx)
{
	ctx->error = BEN_INVALID;
	return NULL;
}

static void *oom_ptr(struct decode *ctx)
{
	ctx->error = BEN_NO_MEMORY;
	return NULL;
}

static struct bencode *decode_bool(struct decode *ctx)
{
	struct bencode_bool *b;
	char value;
	char c;
	if ((ctx->off + 2) > ctx->len)
		return insufficient_ptr(ctx);

	c = ctx->data[ctx->off + 1];
	if (c != '0' && c != '1')
		return invalid_ptr(ctx);

	value = (c == '1');
	b = alloc(BENCODE_BOOL);
	if (b == NULL)
		return oom_ptr(ctx);

	b->b = value;
	ctx->off += 2;
	return (struct bencode *) b;
}

static int resize_dict(struct bencode_dict *d)
{
	struct bencode **newkeys;
	struct bencode **newvalues;
	size_t newsize;

	if (d->alloc >= MAX_ALLOC)
		return -1;

	if (d->alloc == 0)
		d->alloc = 4;
	else
		d->alloc *= 2;
	newsize = sizeof(d->values[0]) * d->alloc;

	newkeys = realloc(d->keys, newsize);
	newvalues = realloc(d->values, newsize);
	if (newkeys == NULL || newvalues == NULL) {
		free(newkeys);
		free(newvalues);
		return -1;
	}
	d->keys = newkeys;
	d->values = newvalues;
	return 0;
}

int ben_cmp(const struct bencode *a, const struct bencode *b)
{
	size_t cmplen;
	int ret;
	const struct bencode_str *sa;
	const struct bencode_str *sb;

	if (a->type != b->type)
		return (a->type == BENCODE_INT) ? -1 : 1;

	if (a->type == BENCODE_INT) {
		const struct bencode_int *ia = ben_int_const_cast(a);
		const struct bencode_int *ib = ben_int_const_cast(b);
		if (ia->ll < ib->ll)
			return -1;
		if (ib->ll < ia->ll)
			return 1;
		return 0;
	}

	sa = ben_str_const_cast(a);
	sb = ben_str_const_cast(b);
	cmplen = (sa->len <= sb->len) ? sa->len : sb->len;
	ret = memcmp(sa->s, sb->s, cmplen);
	if (sa->len == sb->len)
		return ret;
	if (ret)
		return ret;
	return (sa->len < sb->len) ? -1 : 1;
}

int ben_cmp_qsort(const void *a, const void *b)
{
	const struct bencode *akey = ((const struct bencode_keyvalue *) a)->key;
	const struct bencode *bkey = ((const struct bencode_keyvalue *) b)->key;
	return ben_cmp(akey, bkey);
}

static struct bencode *decode_dict(struct decode *ctx)
{
	struct bencode *key;
	struct bencode *value;
	struct bencode_dict *d;

	d = alloc(BENCODE_DICT);
	if (d == NULL) {
		fprintf(stderr, "bencode: Not enough memory for dict\n");
		return oom_ptr(ctx);
	}

	ctx->off += 1;

	while (ctx->off < ctx->len && ctx->data[ctx->off] != 'e') {
		if (d->n == d->alloc && resize_dict(d)) {
			fprintf(stderr, "bencode: Can not resize dict\n");
			ctx->error = BEN_NO_MEMORY;
			goto error;
		}
		key = decode(ctx);
		if (key == NULL)
			goto error;
		if (key->type != BENCODE_INT && key->type != BENCODE_STR) {
			ben_free(key);
			key = NULL;
			ctx->error = BEN_INVALID;
			fprintf(stderr, "bencode: Invalid dict key type\n");
			goto error;
		}
		if (d->n > 0 && ben_cmp(d->keys[d->n - 1], key) >= 0) {
			ben_free(key);
			key = NULL;
			ctx->error = BEN_INVALID;
			goto error;
		}

		value = decode(ctx);
		if (value == NULL) {
			ben_free(key);
			key = NULL;
			goto error;
		}
		d->keys[d->n] = key;
		d->values[d->n] = value;
		d->n++;
	}
	if (ctx->off >= ctx->len) {
		ctx->error = BEN_INSUFFICIENT;
		goto error;
	}

	ctx->off += 1;

	return (struct bencode *) d;

error:
	ben_free((struct bencode *) d);
	return NULL;
}

/* off is the position of first number in */
static int read_long_long(long long *ll, struct decode *ctx, int c)
{
	char buf[LONGLONGSIZE]; /* fits all 64 bit integers */
	size_t pos;
	char *endptr;
	size_t slen;

	pos = find(ctx->data, ctx->len, ctx->off, c);
	if (pos == -1)
		return insufficient(ctx);

	slen = pos - ctx->off;
	if (slen == 0 || slen >= sizeof buf)
		return invalid(ctx);

	assert(slen < sizeof buf);
	memcpy(buf, ctx->data + ctx->off, slen);
	buf[slen] = 0;

	if (buf[0] != '-' && !isdigit(buf[0]))
		return invalid(ctx);

	errno = 0;
	*ll = strtoll(buf, &endptr, 10);
	if (errno == ERANGE || *endptr != 0)
		return invalid(ctx);

	/*
	 * Demand a unique encoding for all integers.
	 * Zero may not begin with a (minus) sign.
	 * Non-zero integers may not have leading zeros in the encoding.
	 */
	if (buf[0] == '-' && buf[1] == '0')
		return invalid(ctx);
	if (buf[0] == '0' && pos != (ctx->off + 1))
		return invalid(ctx);

	ctx->off = pos + 1;
	return 0;
}

static struct bencode *decode_int(struct decode *ctx)
{
	struct bencode_int *b;
	long long ll;
	ctx->off += 1;
	if (read_long_long(&ll, ctx, 'e'))
		return NULL;
	b = alloc(BENCODE_INT);
	if (b == NULL)
		return oom_ptr(ctx);
	b->ll = ll;
	return (struct bencode *) b;
}

static int resize_list(struct bencode_list *list)
{
	struct bencode **newvalues;
	size_t newsize;

	if (list->alloc >= MAX_ALLOC)
		return -1;

	if (list->alloc == 0)
		list->alloc = 4;
	else
		list->alloc *= 2;
	newsize = sizeof(list->values[0]) * list->alloc;

	newvalues = realloc(list->values, newsize);
	if (newvalues == NULL)
		return -1;
	list->values = newvalues;
	return 0;
}

static struct bencode *decode_list(struct decode *ctx)
{
	struct bencode_list *l = alloc(BENCODE_LIST);
	if (l == NULL)
		return oom_ptr(ctx);

	ctx->off += 1;

	while (ctx->off < ctx->len && ctx->data[ctx->off] != 'e') {
		struct bencode *b = decode(ctx);
		if (b == NULL)
			goto error;
		if (ben_list_append((struct bencode *) l, b)) {
			ben_free(b);
			ctx->error = BEN_NO_MEMORY;
			goto error;
		}
	}

	if (ctx->off >= ctx->len) {
		ctx->error = BEN_INSUFFICIENT;
		goto error;
	}

	ctx->off += 1;
	return (struct bencode *) l;

error:
	ben_free((struct bencode *) l);
	return NULL;
}

static size_t read_size_t(struct decode *ctx, int c)
{
	long long ll;
	size_t s;
	if (read_long_long(&ll, ctx, c))
		return -1;
	if (ll < 0)
		return invalid(ctx);
	/*
	 * Test that information is not lost when converting from long long
	 * to size_t
	 */
	s = (size_t) ll;
	if (ll != (long long) s)
		return invalid(ctx);
	return s;
}

static struct bencode *decode_str(struct decode *ctx)
{
	struct bencode *b;
	size_t datalen = read_size_t(ctx, ':'); /* Read the string length */
	if (datalen == -1)
		return NULL;

	if ((ctx->off + datalen) > ctx->len)
		return insufficient_ptr(ctx);

	/* Allocate string structure and copy data into it */
	b = ben_blob(ctx->data + ctx->off, datalen);
	ctx->off += datalen;
	return b;
}

static struct bencode *decode(struct decode *ctx)
{
	ctx->level++;
	if (ctx->level > 256)
		return invalid_ptr(ctx);

	if (ctx->off == ctx->len)
		return insufficient_ptr(ctx);

	assert (ctx->off < ctx->len);
	switch (ctx->data[ctx->off]) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return decode_str(ctx);
	case 'b':
		return decode_bool(ctx);
	case 'd':
		return decode_dict(ctx);
	case 'i':
		return decode_int(ctx);
	case 'l':
		return decode_list(ctx);
	default:
		return invalid_ptr(ctx);
	}
}

struct bencode *ben_decode(const void *data, size_t len)
{
	struct decode ctx = {.data = data, .len = len};
	struct bencode *b = decode(&ctx);
	if (b != NULL && ctx.off != len) {
		ben_free(b);
		return NULL;
	}
	return b;
}

struct bencode *ben_decode2(const void *data, size_t len, size_t *off, int *error)
{
	struct decode ctx = {.data = data, .len = len, .off = *off};
	struct bencode *b = decode(&ctx);
	*off = ctx.off;
	if (error != NULL) {
		assert((b != NULL) ^ (ctx.error != 0));
		*error = ctx.error;
	}
	return b;
}

static void free_dict(struct bencode_dict *d)
{
	size_t pos;
	for (pos = 0; pos < d->n; pos++) {
		ben_free(d->keys[pos]);
		d->keys[pos] = NULL;
		ben_free(d->values[pos]);
		d->values[pos] = NULL;
	}
}

static void free_list(struct bencode_list *list)
{
	size_t pos;
	for (pos = 0; pos < list->n; pos++) {
		ben_free(list->values[pos]);
		list->values[pos] = NULL;
	}
}

static int putonechar(char *data, size_t size, size_t *pos, char c)
{
	if (*pos >= size)
		return -1;
	data[*pos] = c;
	*pos += 1;
	return 0;
}

static int puthexchar(char *data, size_t size, size_t *pos, unsigned char hex)
{
	char buf[5];
	int len = snprintf(buf, sizeof buf, "\\x%.2x", hex);
	assert(len == 4);
	if ((*pos + len) > size)
		return -1;
	memcpy(data + *pos, buf, len);
	*pos += len;
	return 0;
}

static int putlonglong(char *data, size_t size, size_t *pos, long long ll)
{
	char buf[LONGLONGSIZE];
	int len = snprintf(buf, sizeof buf, "%lld", ll);
	assert(len > 0);
	if ((*pos + len) > size)
		return -1;
	memcpy(data + *pos, buf, len);
	*pos += len;
	return 0;
}

static int putunsignedlonglong(char *data, size_t size, size_t *pos,
			       unsigned long long llu)
{
	char buf[LONGLONGSIZE];
	int len = snprintf(buf, sizeof buf, "%llu", llu);
	assert(len > 0);
	if ((*pos + len) > size)
		return -1;
	memcpy(data + *pos, buf, len);
	*pos += len;
	return 0;
}

static int putstr(char *data, size_t size, size_t *pos, char *s)
{
	size_t len = strlen(s);
	if (*pos + len > size)
		return -1;
	memcpy(data + *pos, s, len);
	*pos += len;
	return 0;
}

static int print(char *data, size_t size, size_t *pos, const struct bencode *b)
{
	const struct bencode_bool *boolean;
	const struct bencode_dict *dict;
	const struct bencode_int *integer;
	const struct bencode_list *list;
	const struct bencode_str *s;
	size_t i;
	int len;
	struct bencode_keyvalue *pairs;

	switch (b->type) {
	case BENCODE_BOOL:
		boolean = ben_bool_const_cast(b);
		len = boolean->b ? 4 : 5;
		if (*pos + len > size)
			return -1;
		memcpy(data + *pos, (len == 4) ? "True" : "False", len);
		*pos += len;
		return 0;

	case BENCODE_DICT:
		if (putonechar(data, size, pos, '{'))
			return -1;

		dict = ben_dict_const_cast(b);

		pairs = malloc(dict->n * sizeof(pairs[0]));
		if (pairs == NULL) {
			fprintf(stderr, "bencode: No memory for dict serialization\n");
			return -1;
		}
		for (i = 0; i < dict->n; i++) {
			pairs[i].key = dict->keys[i];
			pairs[i].value = dict->values[i];
		}
		qsort(pairs, dict->n, sizeof(pairs[0]), ben_cmp_qsort);

		for (i = 0; i < dict->n; i++) {
			if (print(data, size, pos, pairs[i].key))
				break;
			if (putstr(data, size, pos, ": "))
				break;
			if (print(data, size, pos, pairs[i].value))
				break;
			if (i < (dict->n - 1)) {
				if (putstr(data, size, pos, ", "))
					break;
			}
		}
		free(pairs);
		pairs = NULL;
		if (i < dict->n)
			return -1;

		return putonechar(data, size, pos, '}');

	case BENCODE_INT:
		integer = ben_int_const_cast(b);

		if (putlonglong(data, size, pos, integer->ll))
			return -1;

		return 0;

	case BENCODE_LIST:
		if (putonechar(data, size, pos, '['))
			return -1;
		list = ben_list_const_cast(b);
		for (i = 0; i < list->n; i++) {
			if (print(data, size, pos, list->values[i]))
				return -1;
			if (i < (list->n - 1) && putstr(data, size, pos, ", "))
				return -1;
		}
		return putonechar(data, size, pos, ']');

	case BENCODE_STR:
		s = ben_str_const_cast(b);
		if (putonechar(data, size, pos, '\''))
			return -1;
		for (i = 0; i < s->len; i++) {
			if (!isprint(s->s[i])) {
				if (puthexchar(data, size, pos, s->s[i]))
					return -1;
				continue;
			}

			switch (s->s[i]) {
			case '\'':
			case '\\':
				/* Need escape character */
				if (putonechar(data, size, pos, '\\'))
					return -1;
			default:
				if (putonechar(data, size, pos, s->s[i]))
					return -1;
				break;
			}
		}
		return putonechar(data, size, pos, '\'');
	default:
		fprintf(stderr, "bencode: serialization type %d not implemented\n", b->type);
		abort();
	}
}

static size_t get_printed_size(const struct bencode *b)
{
	size_t pos;
	const struct bencode_bool *boolean;
	const struct bencode_dict *d;
	const struct bencode_int *i;
	const struct bencode_list *l;
	const struct bencode_str *s;
	size_t size = 0;
	char buf[1];

	switch (b->type) {
	case BENCODE_BOOL:
		boolean = ben_bool_const_cast(b);
		return boolean->b ? 4 : 5; /* "True" and "False" */
	case BENCODE_DICT:
		size += 1; /* "{" */
		d = ben_dict_const_cast(b);
		for (pos = 0; pos < d->n; pos++) {
			size += get_printed_size(d->keys[pos]);
			size += 2; /* ": " */
			size += get_printed_size(d->values[pos]);
			if (pos < (d->n - 1))
				size += 2; /* ", " */
		}
		size += 1; /* "}" */
		return size;
	case BENCODE_INT:
		i = ben_int_const_cast(b);
		return snprintf(buf, 0, "%lld", i->ll);
	case BENCODE_LIST:
		size += 1; /* "[" */
		l = ben_list_const_cast(b);
		for (pos = 0; pos < l->n; pos++) {
			size += get_printed_size(l->values[pos]);
			if (pos < (l->n - 1))
				size += 2; /* ", " */
		}
		size += 1; /* "]" */
		return size;
	case BENCODE_STR:
		s = ben_str_const_cast(b);
		size += 1; /* ' */
		for (pos = 0; pos < s->len; pos++) {
			if (!isprint(s->s[pos])) {
				size += 4; /* "\xDD" */
				continue;
			}
			switch (s->s[pos]) {
			case '\'':
			case '\\':
				size += 2; /* escaped characters */
				break;
			default:
				size += 1;
				break;
			}
		}
		size += 1; /* ' */
		return size;
	default:
		fprintf(stderr, "bencode: invalid bencode type: %c\n", b->type);
		abort();
	}
}

static int serialize(char *data, size_t size, size_t *pos,
		     const struct bencode *b)
{
	const struct bencode_dict *dict;
	const struct bencode_int *integer;
	const struct bencode_list *list;
	const struct bencode_str *s;
	size_t i;
	struct bencode_keyvalue *pairs;

	switch (b->type) {
	case BENCODE_BOOL:
		if ((*pos + 2) > size)
			return -1;
		data[*pos] = 'b';
		data[*pos + 1] = ben_bool_const_cast(b)->b ? '1' : '0';
		*pos += 2;
		return 0;

	case BENCODE_DICT:
		if (putonechar(data, size, pos, 'd'))
			return -1;

		dict = ben_dict_const_cast(b);

		pairs = malloc(dict->n * sizeof(pairs[0]));
		if (pairs == NULL) {
			fprintf(stderr, "bencode: No memory for dict serialization\n");
			return -1;
		}
		for (i = 0; i < dict->n; i++) {
			pairs[i].key = dict->keys[i];
			pairs[i].value = dict->values[i];
		}
		qsort(pairs, dict->n, sizeof(pairs[0]), ben_cmp_qsort);

		for (i = 0; i < dict->n; i++) {
			if (serialize(data, size, pos, pairs[i].key))
				break;
			if (serialize(data, size, pos, pairs[i].value))
				break;
		}
		free(pairs);
		pairs = NULL;
		if (i < dict->n)
			return -1;

		return putonechar(data, size, pos, 'e');

	case BENCODE_INT:
		if (putonechar(data, size, pos, 'i'))
			return -1;
		integer = ben_int_const_cast(b);
		if (putlonglong(data, size, pos, integer->ll))
			return -1;
		return putonechar(data, size, pos, 'e');

	case BENCODE_LIST:
		if (putonechar(data, size, pos, 'l'))
			return -1;

		list = ben_list_const_cast(b);
		for (i = 0; i < list->n; i++) {
			if (serialize(data, size, pos, list->values[i]))
				return -1;
		}

		return putonechar(data, size, pos, 'e');

	case BENCODE_STR:
		s = ben_str_const_cast(b);
		if (putunsignedlonglong(data, size, pos, ((long long) s->len)))
			return -1;
		if (putonechar(data, size, pos, ':'))
			return -1;
		if ((*pos + s->len) > size)
			return -1;
		memcpy(data + *pos, s->s, s->len);
		*pos += s->len;
		return 0;

	default:
		fprintf(stderr, "bencode: serialization type %d not implemented\n", b->type);
		abort();
	}
}

static size_t get_size(const struct bencode *b)
{
	size_t pos;
	const struct bencode_dict *d;
	const struct bencode_int *i;
	const struct bencode_list *l;
	const struct bencode_str *s;
	size_t size = 0;
	char buf[1];

	switch (b->type) {
	case BENCODE_BOOL:
		return 2;
	case BENCODE_DICT:
		d = ben_dict_const_cast(b);
		for (pos = 0; pos < d->n; pos++) {
			size += get_size(d->keys[pos]);
			size += get_size(d->values[pos]);
		}
		return size + 2;
	case BENCODE_INT:
		i = ben_int_const_cast(b);
		return 2 + snprintf(buf, 0, "%lld", i->ll);
	case BENCODE_LIST:
		l = ben_list_const_cast(b);
		for (pos = 0; pos < l->n; pos++)
			size += get_size(l->values[pos]);
		return size + 2;
	case BENCODE_STR:
		s = ben_str_const_cast(b);
		return snprintf(buf, 0, "%zu", s->len) + 1 + s->len;
	default:
		fprintf(stderr, "bencode: invalid bencode type: %c\n", b->type);
		abort();
	}
}

size_t ben_encoded_size(const struct bencode *b)
{
	return get_size(b);
}

void *ben_encode(size_t *len, const struct bencode *b)
{
	size_t size = get_size(b);
	void *data = malloc(size);
	if (data == NULL) {
		fprintf(stderr, "bencode: No memory to encode\n");
		return NULL;
	}
	*len = 0;
	if (serialize(data, size, len, b)) {
		free(data);
		return NULL;
	}
	assert(*len == size);
	return data;
}

size_t ben_encode2(char *data, size_t maxlen, const struct bencode *b)
{
	size_t pos = 0;
	if (serialize(data, maxlen, &pos, b))
		return -1;
	return pos;
}

void ben_free(struct bencode *b)
{
	if (b == NULL)
		return;
	switch (b->type) {
	case BENCODE_BOOL:
		break;
	case BENCODE_DICT:
		free_dict((struct bencode_dict *) b);
		break;
	case BENCODE_INT:
		break;
	case BENCODE_LIST:
		free_list((struct bencode_list *) b);
		break;
	case BENCODE_STR:
		free(((struct bencode_str *) b)->s);
		break;
	default:
		fprintf(stderr, "bencode: invalid type: %d\n", b->type);
		abort();
	}

	memset(b, -1, type_size(b->type)); /* data poison */
	free(b);
}

struct bencode *ben_blob(const void *data, size_t len)
{
	struct bencode_str *b = alloc(BENCODE_STR);
	if (b == NULL)
		return NULL;
	/* Allocate one extra byte for zero termination for convenient use */
	b->s = malloc(len + 1);
	if (b->s == NULL) {
		free(b);
		return NULL;
	}
	memcpy(b->s, data, len);
	b->len = len;
	b->s[len] = 0;
	return (struct bencode *) b;
}

struct bencode *ben_bool(int boolean)
{
	struct bencode_bool *b = alloc(BENCODE_BOOL);
	if (b == NULL)
		return NULL;
	b->b = boolean ? 1 : 0;
	return (struct bencode *) b;
}

struct bencode *ben_dict(void)
{
	return alloc(BENCODE_DICT);
}

struct bencode *ben_dict_get(const struct bencode *dict, const struct bencode *key)
{
	const struct bencode_dict *d = ben_dict_const_cast(dict);
	size_t pos;
	for (pos = 0; pos < d->n; pos++) {
		if (ben_cmp(d->keys[pos], key) == 0)
			return d->values[pos];
	}
	return NULL;
}

struct bencode *ben_dict_get_by_str(const struct bencode *dict, const char *key)
{
	const struct bencode_dict *d = ben_dict_const_cast(dict);
	size_t keylen = strlen(key);
	struct bencode_str *dkey;
	size_t pos;
	for (pos = 0; pos < d->n; pos++) {
		dkey = ben_str_cast(d->keys[pos]);
		if (dkey == NULL)
			continue;
		if (dkey->len != keylen)
			continue;
		if (strcmp(dkey->s, key) == 0)
			return d->values[pos];
	}
	return NULL;
}

static void replacewithlast(struct bencode **arr, size_t i, size_t n)
{
	arr[i] = arr[n - 1];
	arr[n - 1] = NULL;
}

struct bencode *ben_dict_pop(struct bencode *dict, const struct bencode *key)
{
	struct bencode_dict *d = ben_dict_cast(dict);
	size_t pos;
	for (pos = 0; pos < d->n; pos++) {
		if (ben_cmp(d->keys[pos], key) == 0) {
			struct bencode *value = d->values[pos];
			ben_free(d->keys[pos]);
			replacewithlast(d->keys, pos, d->n);
			replacewithlast(d->values, pos, d->n);
			d->n -= 1;
			return value;
		}
	}
	return NULL;
}

int ben_dict_set(struct bencode *dict, struct bencode *key, struct bencode *value)
{
	struct bencode_dict *d = ben_dict_cast(dict);

	assert(d->n <= d->alloc);
	if (d->n == d->alloc && resize_dict(d))
		return -1;

	ben_free(ben_dict_pop(dict, key));

	d->keys[d->n] = key;
	d->values[d->n] = value;
	d->n++;
	return 0;
}

int ben_dict_set_by_str(struct bencode *dict, const char *key, struct bencode *value)
{
	struct bencode *bkey = ben_str(key);
	if (bkey == NULL)
		return -1;
	if (ben_dict_set(dict, bkey, value)) {
		ben_free(bkey);
		return -1;
	}
	return 0;
}

int ben_dict_set_str_by_str(struct bencode *dict, const char *key, const char *value)
{
	struct bencode *bkey = ben_str(key);
	struct bencode *bvalue = ben_str(value);
	if (bkey == NULL || bvalue == NULL) {
		ben_free(bkey);
		ben_free(bvalue);
		return -1;
	}
	if (ben_dict_set(dict, bkey, bvalue)) {
		ben_free(bkey);
		ben_free(bvalue);
		return -1;
	}
	return 0;
}

struct bencode *ben_int(long long ll)
{
	struct bencode_int *b = alloc(BENCODE_INT);
	if (b == NULL)
		return NULL;
	b->ll = ll;
	return (struct bencode *) b;
}

struct bencode *ben_list(void)
{
	return alloc(BENCODE_LIST);
}

int ben_list_append(struct bencode *list, struct bencode *b)
{
	struct bencode_list *l = ben_list_cast(list);
	/* NULL pointer de-reference if the cast fails */
	assert(l->n <= l->alloc);
	if (l->n == l->alloc && resize_list(l))
		return -1;
	l->values[l->n] = b;
	l->n += 1;
	return 0;
}

void ben_list_set(struct bencode *list, size_t i, struct bencode *b)
{
	struct bencode_list *l = ben_list_cast(list);
	if (i >= l->n) {
		fprintf(stderr, "bencode: ben_list_set() out of bounds: %zu\n", i);
		abort();
	}
	ben_free(l->values[i]);
	l->values[i] = b;
}

char *ben_print(const struct bencode *b)
{
	size_t size = get_printed_size(b);
	char *data = malloc(size + 1);
	size_t len = 0;
	if (data == NULL) {
		fprintf(stderr, "bencode: No memory to print\n");
		return NULL;
	}
	if (print(data, size, &len, b)) {
		free(data);
		return NULL;
	}
	assert(len == size);
	data[size] = 0;
	return data;
}

struct bencode *ben_str(const char *s)
{
	return ben_blob(s, strlen(s));
}

const char *ben_strerror(int error)
{
	switch (error) {
	case BEN_OK:
		return "OK (no error)";
	case BEN_INVALID:
		return "Invalid data";
	case BEN_INSUFFICIENT:
		return "Insufficient amount of data (need more data)";
	case BEN_NO_MEMORY:
		return "Out of memory";
	default:
		fprintf(stderr, "Unknown error code: %d\n", error);
		return NULL;
	}
}
