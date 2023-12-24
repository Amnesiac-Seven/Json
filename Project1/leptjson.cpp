#ifndef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include<crtdbg.h>
#endif

#include"leptjson.h"
#include<iostream>
#include<errno.h>
#include<stdio.h>
#include<math.h>
#include<string.h>
#include <stdlib.h>
#include<cassert>

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif // !LEPT_PARSE_STACK_INIT_SIZE stack初始值

#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256
#endif // !LEPT_PARSE_STRINGIFY_INIT_SIZE stringify初始值


//void* lept_context_push(lept_context* c, size_t size);
#define EXPECT(c, ch)	do{assert(*c->json==ch);c->json++;}while(0)/* 从字符串c中删除字符ch */
#define ISDIGIT(ch)		(ch >= '0' && ch <= '9')/* 判断字符ch是否是数字 */
#define ISDIGIT1TO9(ch) (ch >= '1' && ch <= '9')
#define PUTC(c, ch)		do { *(char*)lept_context_push(c, sizeof(char)) = (ch); } while(0)/* 从c开辟空间，并赋值为字符ch */
#define PUTS(c, s, len) memcpy(lept_context_push(c, len), s, len) /* 从字符串c开辟len空间长度，将s的前len个字符赋值到c中 */

typedef struct {
	const char* json;
	char* stack;
	size_t size, top;// size为stack的容量，top为stack当前存放字符个数
}lept_context;

// 在stack中拓展size个字节，返回待写入字符的位置，以及更新c->top
static void* lept_context_push(lept_context* c, size_t size) {
	void* ret;
	assert(size > 0);
	if (c->top + size >= c->size) {
		if (c->size == 0)c->size = LEPT_PARSE_STACK_INIT_SIZE;
		while (c->top + size >= c->size)// 按照1.5倍扩展c的大小
			c->size += c->size >> 1;
		c->stack = (char*)realloc(c->stack, c->size);// 将c->stack中的c->size字符全部转移到新位置
	}
	ret = c->stack + c->top;// ret为新输入字符串的位置
	c->top += size;			// 更新stack的长度
	return ret;
}

// 从c->stack中移除size个字符，更新top位置，并返回字符待写入的位置
static void* lept_context_pop(lept_context* c, size_t size) {
	assert(c->top >= size);
	return c->stack + (c->top -= size);
}

// 清除c->json字符串前端的' '、'\r'、'\t'、'\n'字符
static void lept_parse_whiteSpace(lept_context *c) {
	const char *p = c->json;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')++p;
	c->json = p;
}

// 判断c->json与literal是否相同，如果相同，则v->type = type并返回LEPT_PARSE_OK，否则返回LEPT_PARSE_INVALID_VALUE
int lept_parse_literal(lept_context* c, lept_value* v, const char* literal, lept_type type) {
	assert(v != NULL);
	// if (sizeof c->json != sizeof literal || memcmp(c->json, literal, sizeof literal) != 0)return LEPT_PARSE_INVALID_VALUE;
	size_t i;
	EXPECT(c, literal[0]);
	for (i = 0; literal[i + 1]; i++) {
		if (c->json[i] != literal[i + 1])
			return LEPT_PARSE_INVALID_VALUE;
	}
	c->json += i;
	v->type = type;
	return LEPT_PARSE_OK;
}

// 判断c->json中字符串是否满足数字要求，包含小数、整数、科学计数法等，若满足要求则更新v->type/v->u.n
static int lept_parse_number(lept_context* c, lept_value* v) {
	const char* p = c->json;
	if (*p == '-')p++;
	if (*p == '0')p++;// 必定是小数，否则返回
	else {
		if (!ISDIGIT1TO9(*p))return LEPT_PARSE_INVALID_VALUE;// 如e
		for (p++; ISDIGIT(*p); p++);
	}
	if (*p == '.') {//小数可将.前后看为2个整数
		p++;
		if (!ISDIGIT(*p))return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++);
	}
	if (*p == 'e' || *p == 'E') {
		p++;
		if (*p == '+' || *p == '-')p++;
		if (!ISDIGIT(*p))return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++);
	}
	errno = 0;
	// std::cout << strtod("0123", NULL) << std::endl;
	v->u.n = strtod(c->json, NULL);
	// strtod()正确的值超出该类型可表示的值的范围，则为正或负HUGE_VAL返回，并且errno被设定为ERANGE。
	if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
		return LEPT_PARSE_NUMBER_TOO_BIG;// 超出范围，则返回
	v->type = LEPT_NUMBER;
	c->json = p;// 更新c->json到符合数字编写要求的后一位，如0123则c->json定位到字符1处
	return LEPT_PARSE_OK;
}

// 读取*p四位字符，并存到u中
static const char* lept_parse_hex4(const char* p, unsigned* u) {
	int i = 0;
	*u = 0;
	for (i = 0; i < 4; i++) {
		char ch = *p++;
		*u <<= 4;// 16进制(0~F)，所以每次扩展4位
		if	    (ch >= '0' && ch <= '9')*u |= ch - '0';
		else if (ch >= 'A' && ch <= 'F')*u |= ch - ('A' - 10);
		else if (ch >= 'a' && ch <= 'f')*u |= ch - ('a' - 10);
		else return nullptr;
	}
	return p;
}

// 按照uft8要求，将u存入c中
void lept_encode_utf8(lept_context* c, unsigned u) {
	if (u <= 0x007F)
		PUTC(c, (0x7F & u));
	else if (u <= 0x07FF) {
		PUTC(c, (0xC0 | ((u >> 6) & 0xFF))); /* 0xC0 = 11000000 0x1F = 00011111 */
		PUTC(c, (0x80 | (u        & 0x3F))); /* 0x80 = 10000000 0x3F = 00311111 */
	}
	else if (u <= 0xFFFF) {
		PUTC(c, (0xE0 | ((u >> 12) & 0xFF))); /* 0xE0 = 11100000 */
		PUTC(c, (0x80 | ((u >> 6)  & 0x3F))); /* 0x80 = 10000000 */
		PUTC(c, (0x80 | (u		   & 0x3F))); /* 0x3F = 00111111 */
	}
	else if (u <= 0x10FFFF) {
		PUTC(c, (0xF0 | ((u >> 18) & 0xFF)));
		PUTC(c, (0x80 | ((u >> 12) & 0x3F))); /* 0x80 = 10000000 */
		PUTC(c, (0x80 | ((u >> 6)  & 0x3F))); /* 0x80 = 10000000 */
		PUTC(c, (0x80 | (u         & 0x3F))); /* 0x3F = 00111111 */
	}
}

#define STRING_ERROR(ret) do{ c->top = head; return ret; }while(0)

// 将c中的字符全部读入*str，长度写入*len
static int lept_parse_string_raw(lept_context* c, char** str, size_t* len) {
	size_t head = c->top;
	unsigned u, u2;
	const char* p;
	EXPECT(c, '\"');
	p = c->json;
	for (;;) {
		char ch = *p++;
		switch (ch){
			case '\"':// 字符串已全部读取，更新*str和*len
				*len = c->top - head;
				*str = (char*)lept_context_pop(c, *len);
				c->json = p;
				return LEPT_PARSE_OK;
			case '\\':// 判断特殊字符
				switch (*p++) {
					case '\"':	PUTC(c, '\"'); break;
					case '\\':	PUTC(c, '\\'); break;
					case '/':	PUTC(c, '/'); break;
					case 'b':	PUTC(c, '\b'); break;
					case 'f':	PUTC(c, '\f'); break;
					case 'n':	PUTC(c, '\n'); break;
					case 'r':	PUTC(c, '\r'); break;
					case 't':	PUTC(c, '\t'); break;
					case 'u':
						if (!(p = lept_parse_hex4(p, &u)))
							STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
						// 判断u是否为高代理项，如果是高代理项则形如:`\uD834\uDD1E`
						if (u >= 0xD800 && u <= 0xDBFF) {
							if (*p++ != '\\')
								STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
							if (*p++ != 'u')
								STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
							if (!(p = lept_parse_hex4(p, &u2)))
								STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
							// U+DC00 至 U+DFFF 的低代理项
							if (u2 < 0xDC00 || u2 > 0xDFFF)
								STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
							// codepoint = 0x10000 + (H − 0xD800) × 0x400 + (L − 0xDC00) H:高代理项 L:低代理项
							u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
						}
						lept_encode_utf8(c, u);
						break;
					default:
						STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
				}
				break;
			case '\0':
				STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
			default:
				if ((unsigned char)ch < 0x20) {
					STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
				}
				PUTC(c, ch);
		}
	}
}

// 将c中符合要求的字符串全部写入*s，字符串长度len，并赋值给v中元素
static int lept_parse_string(lept_context* c, lept_value* v) {
	int ret;
	char* s;
	size_t len;
	if ((ret = lept_parse_string_raw(c, &s, &len)) == LEPT_PARSE_OK)
		lept_set_string(v, s, len);
	return ret;
}

// 统计c中数组信息，并赋值到v中
static int lept_parse_value(lept_context* c, lept_value* v);
static int lept_parse_array(lept_context* c, lept_value* v) {
	size_t i, size = 0;
	int ret = 0;
	EXPECT(c, '[');
	lept_parse_whiteSpace(c);
	if (*c->json == ']') {// 针对'[]'这种数据输入
		c->json++;
		v->type = LEPT_ARRAY;
		v->u.a.e = NULL;
		v->u.a.size = 0;
		return LEPT_PARSE_OK;
	}
	for (;;) {
		lept_value e;
		lept_init(&e);
		if ((ret = lept_parse_value(c, &e)) != LEPT_PARSE_OK)// 将c中信息写入e中
			break;
		memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));// 将e赋值到c中stack新开辟的位置处
		size++;
		lept_parse_whiteSpace(c);
		if (*c->json == ',') {// 直接跳过','
			c->json++;
			lept_parse_whiteSpace(c);
		}
		else if (*c->json == ']') {// ']'达到右边界，完成整个数组的分析
			c->json++;
			v->type = LEPT_ARRAY;
			v->u.a.size = size;
			size *= sizeof(lept_value);
			// 将c中最后长度size字节赋值到v->u.a.e中新开辟size字节空间中
			memcpy(v->u.a.e = (lept_value*)malloc(size), lept_context_pop(c, size), size);
			return LEPT_PARSE_OK;
		}
		else {// 主要用于检测缺少右侧']'的情况
			ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
			break;
		}
	}
	for (i = 0; i < size; i++)// pop并释放stack中所有元素(针对236行开辟新区间的动作)
		lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));
	return ret;
}

// 统计c中的对象信息，并全部赋值到v中
static int lept_parse_object(lept_context* c, lept_value* v) {
	EXPECT(c, '{');
	lept_parse_whiteSpace(c);
	if (*c->json == '}') {// 类型:{}里面只有空格字符的对象
		c->json++;
		v->type = LEPT_OBJECT;
		v->u.o.size = 0;
		v->u.o.m = 0;
		return LEPT_PARSE_OK;
	}
	lept_member m;
	size_t size = 0;
	m.k = nullptr;
	int ret;
	for (;;) {
		char* str;
		lept_init(&m.v);
		if (*c->json != '"') {// 对象缺少key值
			ret = LEPT_PARSE_MISS_KEY;
			break;
		}
		// lept_parse_string_raw将c中的字符全部读入*str，长度写入*len
		if ((ret = lept_parse_string_raw(c, &str, &m.len)) != LEPT_PARSE_OK)
			break;

		memcpy(m.k = (char*)malloc(m.len + 1), str, m.len);// 将str赋值给m.k
		m.k[m.len] = '\0';
		lept_parse_whiteSpace(c);
		if (*c->json != ':') {// 对象缺少value值
			ret = LEPT_PARSE_MISS_COLON;
			break;
		}
		c->json++;
		lept_parse_whiteSpace(c);
		if ((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK)// 将c中value部分赋值给m.v
			break;
		// 在c中stack内开辟一个lept_member区间存放m中数据
		memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(lept_member));
		size++;
		m.k = nullptr;
		lept_parse_whiteSpace(c);
		if (*c->json == ',') {
			c->json++;
			lept_parse_whiteSpace(c);
		}
		else if (*c->json == '}') {
			size_t s = sizeof(lept_member) * size;
			c->json++;
			v->type = LEPT_OBJECT;
			v->u.o.size = size;
			// v->u.o.m中开辟大小为s的区间，存放c中stack最后s个字符
			memcpy(v->u.o.m = (lept_member*)malloc(s), lept_context_pop(c, s), s);
			return LEPT_PARSE_OK;
		}
		else {
			ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
			break;
		}
	}
	free(m.k);// 针对288行的malloc函数
	for (size_t i = 0; i < size; i++) {
		lept_member* m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
		free(m->k);
		lept_free(&m->v);
	}
	v->type = LEPT_NULL;
	return ret;
}

// 根据c->json首字符，判断将c->json中字符能否存放入v中，选择具体数据类型进行进一步判断
static int lept_parse_value(lept_context* c, lept_value* v) {
	// std::cout << *c->json << std::endl;
	switch (*c->json)
	{
		case 'n':	return lept_parse_literal(c, v, "null", LEPT_NULL);
		case 't':   return lept_parse_literal(c, v, "true", LEPT_TRUE);
		case 'f':   return lept_parse_literal(c, v, "false", LEPT_FALSE);
		default:	return lept_parse_number(c, v);
		case '"':	return lept_parse_string(c, v);
		case '[':	return lept_parse_array(c, v);
		case '{':	return lept_parse_object(c, v);
		case '\0':	return LEPT_PARSE_EXPECT_VALUE;
	}
}

// 根据json字符串，如果将v中的元素更新成功并返回LEPT_PARSE_OK，否则返回LEPT_PARSE_ROOT_NOT_SINGULAR
int lept_parse(lept_value* v, const char* json) {
	lept_context c;
	assert(v != nullptr);
	c.json = json;
	c.stack = NULL;
	c.size = c.top = 0;
	lept_init(v);
	lept_parse_whiteSpace(&c);
	int ret;
	if ((ret = lept_parse_value(&c, v)) == LEPT_PARSE_OK) {
		lept_parse_whiteSpace(&c);
		if (*c.json != '\0') {// 针对如"null x/0123/0x123/0x"等情况
			v->type= LEPT_NULL;
			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
		}
	}
	assert(c.top == 0);
	free(c.stack);
	return ret;
}

// 将字符串s中len个字符进行逆转换，写入c中，更新c->top
static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
	assert(s != nullptr); 
	static const char hex_digits[] = { '0','1','2','3','4','5','6','7','8','9' ,'A','B','C','D','E','F' };
	size_t i, size;
	char* head, * p;
	// 每个字符可生成最长的形式是 `\u00XX`，占 6 个字符，再加上前后两个双引号，也就是共 `len * 6 + 2` 个输出字符
	p = head = (char*)lept_context_push(c, size = len * 6 + 2);/* c中开辟size个字节存储"\u00xx..." */
	*p++ = '"';
	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		switch (ch) {
			case '\"':*p++ = '\\'; *p++ = '\"'; break;
			case '\\':*p++ = '\\'; *p++ = '\\'; break;
			case '\b':*p++ = '\\'; *p++ = 'b'; break;
			case '\f':*p++ = '\\'; *p++ = 'f'; break;
			case '\n':*p++ = '\\'; *p++ = 'n'; break;
			case '\r':*p++ = '\\'; *p++ = 'r'; break;
			case '\t':*p++ = '\\'; *p++ = 't'; break;
			default:
				if (ch < 0x20) {
					*p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
					*p++ = hex_digits[ch >> 4];// ch高4位
					*p++ = hex_digits[ch & 15];// ch低4位
				}
				else *p++ = s[i];
		}
	}
	*p++ = '"';
	c->top -= size - (p - head);// 顺序:()、-、-=
}

// 将v中数据写入c中
static void lept_stringify_value(lept_context* c, const lept_value* v) {
	size_t i;
	switch (v->type)
	{
		case LEPT_NULL:		PUTS(c, "null", 4); break;
		case LEPT_TRUE:		PUTS(c, "true", 4); break;
		case LEPT_FALSE:	PUTS(c, "false", 5); break;
		/* 将v->u.n按照"%.17g"格式写入c中新开辟的32个字符首位置，并根据写入数量更新c->top */
		case LEPT_NUMBER:	c->top -= 32 - sprintf_s((char*)lept_context_push(c, 32), 32, "%.17g", v->u.n); break;
		case LEPT_STRING:	lept_stringify_string(c, v->u.s.s, v->u.s.len); break;
		case LEPT_ARRAY:
			PUTC(c, '[');
			for (i = 0; i < v->u.a.size; i++) {
				if (i > 0)PUTC(c, ',');
				lept_stringify_value(c, &v->u.a.e[i]);
			}
			PUTC(c, ']');
			break;
		case LEPT_OBJECT:
			PUTC(c, '{');
			for (i = 0; i < v->u.o.size; i++) {
				if (i > 0)PUTC(c, ',');
				lept_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].len);
				PUTC(c, ':');
				lept_stringify_value(c, &v->u.o.m[i].v);
			}
			PUTC(c, '}');
			break;
		default:
			assert(0 && "invalid type");
	}
}

// 初始化lept_context，将v中数据存入c.stack，并返回
char* lept_stringify(const lept_value* v, size_t* length) {
	lept_context c;
	assert(v != nullptr);
	c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
	c.top = 0;
	lept_stringify_value(&c, v);
	if (length)
		*length = c.top;
	PUTC(&c, '\0');
	return c.stack;
}

// 释放lept_value类型元素空间
void lept_free(lept_value* v) {
	assert(v != nullptr);
	switch (v->type)
	{
		case LEPT_STRING:
			free(v->u.s.s);
			break;
		case LEPT_ARRAY:
			for (size_t i = 0; i < v->u.a.size; i++) 
				lept_free(&v->u.a.e[i]);
			free(v->u.a.e);
			break;
		case LEPT_OBJECT:
			for (size_t i = 0; i < v->u.o.size; i++) {
				free(v->u.o.m[i].k);
				lept_free(&v->u.o.m[i].v);
			}
			free(v->u.o.m);
			break;
		default:
			break;
	}
	v->type = LEPT_NULL;
}

lept_type lept_get_type(const lept_value* v) {
	assert(v != nullptr);
	return v->type;
}

int lept_get_boolean(const lept_value* v) {
	assert(v != nullptr && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
	return v->type == LEPT_TRUE;
}

void lept_set_boolean(lept_value* v, int b) {
	lept_free(v);
	v->type = (b ? LEPT_TRUE : LEPT_FALSE);
}

double lept_get_number(const lept_value* v) {
	assert(v != nullptr && v->type == LEPT_NUMBER);
	return v->u.n;
}

void lept_set_number(lept_value* v, double n) {
	lept_free(v);
	v->type = LEPT_NUMBER;
	v->u.n = n;
}

const char* lept_get_string(const lept_value* v) {
	assert(v != nullptr && v->type == LEPT_STRING);
	return v->u.s.s;
}

size_t lept_get_string_length(const lept_value* v) {
	assert(v != nullptr && v->type == LEPT_STRING);
	return v->u.s.len;
}

// 将s指向的字符串赋给v->u.s.s
void lept_set_string(lept_value* v, const char* s, size_t len) {
	assert(v != nullptr && (s != nullptr || len == 0));
	lept_free(v);
	v->u.s.s = (char*)malloc(len + 1);
	memcpy(v->u.s.s, s, len);
	v->u.s.s[len] = '\0';
	v->u.s.len = len;
	v->type = LEPT_STRING;
}

// 返回v中数组长度
size_t lept_get_array_size(const lept_value* v) {
	assert(v != nullptr && v->type == LEPT_ARRAY);
	return v->u.a.size;
}

// 读取v中数组的第index个元素
lept_value* lept_get_array_element(const lept_value* v, size_t index) {
	assert(v != nullptr && v->type == LEPT_ARRAY);
	assert(index < v->u.a.size);
	return &v->u.a.e[index];
}

// 对象总长度
size_t lept_get_object_size(lept_value* v) {
	assert(v != nullptr && v->type == LEPT_OBJECT);
	return v->u.o.size;
}

// 第index个对象key值
const char* lept_get_object_key(const lept_value* v, size_t index) {
	assert(v != nullptr && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].k;
}

// 第index个对象key值长度
size_t lept_get_object_key_length(const lept_value* v, size_t index) {
	assert(v != nullptr && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].len;
}

// 第index个对象value值
lept_value* lept_get_object_value(const lept_value* v, size_t index) {
	assert(v != nullptr && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return &v->u.o.m[index].v;
}


/*static int lept_parse_null(lept_context* c, lept_value* v) {
	EXPECT(c, 'n');
	if (c->json[0] != 'u' || c->json[1] != 'l' || c->json[2] != 'l')
		return LEPT_PARSE_INVALID_VALUE;
	v->type = LEPT_NULL;
	c->json += 3;
	// delete c;
	return LEPT_PARSE_OK;
}

static int lept_parse_true(lept_context* c, lept_value* v) {
	EXPECT(c, 't');
	if (c->json[0] != 'r' || c->json[1] != 'u' || c->json[2] != 'e')
		return LEPT_PARSE_INVALID_VALUE;
	v->type = LEPT_TRUE;
	c->json += 3;
	return LEPT_PARSE_OK;
}

static int lept_parse_false(lept_context* c, lept_value* v) {
	EXPECT(c, 'f');
	if (c->json[0] != 'a' || c->json[1] != 'l' || c->json[2] != 's' || c->json[3] != 'e')
		return LEPT_PARSE_INVALID_VALUE;
	v->type = LEPT_FALSE;
	c->json += 4;
	return LEPT_PARSE_OK;
}*/