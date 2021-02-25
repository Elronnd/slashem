#include "hack.h"
#include "config.h"
#include "unixconf.h"
#include "color.h"
#include "extern.h"
#include "nhstr.h"

void del_nhs(nhstr *str) {
	free(str->str);
	free(str->style);

	str->str = NULL;
	str->style = NULL;
	str->len = 0;
}

nhstr *nhscatznc(nhstr *str, const char *cat, usize catlen, nhstyle style) {
	str->str = realloc(str->str, (str->len + catlen) * sizeof(glyph_t));
	for (usize i = str->len; i < str->len + catlen; i++) {
		str->str[i] = cat[i - str->len];
	}

	str->style = realloc(str->style, (str->len + catlen) * sizeof(nhstyle));
	for (usize i = str->len; i < str->len + catlen; i++) {
		str->style[i] = style;
	}

	str->len += catlen;
	return str;
}

nhstr *nhscatzc(nhstr *str, const char *cat, nhstyle style) {
	return nhscatznc(str, cat, cat ? strlen(cat) : 0, style);
}

nhstr *nhscatzn(nhstr *str, const char *cat, usize catlen) {
	return nhscatznc(str, cat, catlen, nhstyle_default());
}

nhstr *nhscatz(nhstr *str, const char *cat) {
	return nhscatzn(str, cat, cat ? strlen(cat) : 0);
}

nhstr nhsdup(const nhstr str) {
	nhstr ret;
	ret.str = new(glyph_t, str.len);
	memcpy(ret.str, str.str, str.len * sizeof(glyph_t));

	ret.style = new(nhstyle, str.len);
	memcpy(ret.style, str.style, str.len * sizeof(nhstyle));
	ret.len = str.len;

	return ret;
}

nhstr nhsdupz(const char *str) {
	nhstr ret = {0};
	return *nhscatz(&ret, str);
}

nhstr *nhscat(nhstr *str, const nhstr cat) {
	str->str = realloc(str->str, (str->len + cat.len) * sizeof(glyph_t));
	memmove(str->str + str->len, cat.str, cat.len * sizeof(glyph_t));
	str->style = realloc(str->style, (str->len + cat.len) * sizeof(nhstyle));
	memmove(str->style + str->len, cat.style, cat.len * sizeof(nhstyle));

	str->len += cat.len;
	return str;
}

// limited at the moment
// %s: nhstr
// %S: char*
// %c: glyph_t (unicode conversion)
// %l: long
// %i: int
// %u: uint
// %%: literal %
//
// Modifiers:
// %/X: same as %X, but destroy the object once you're done.
//	Only implemented for nhstr (via del_nhs) and char* (via free)
nhstr *nhscatfc_v(nhstr *str, nhstyle style, const char *cat, va_list the_args) {
	for (usize i = 0; cat[i]; i++) {
		if (cat[i] == '%') {
			i++;

			bool should_destroy = false;
			if (cat[i] == '/') { should_destroy = true; i++; }

			bool left_justify = false; // currently only supported for s
			if (cat[i] == '-') { left_justify = true; i++; }

			int num1 = -1, num2 = -1; // currently only supported for %l,u,i
			if (digit(cat[i])) { num1 = 0; while (digit(cat[i])) { num1 *= 10; num1 += cat[i] - '0'; i++; } }
			if (cat[i] == '.' && digit(cat[i+1])) {
				i++;
				num2 = 0;
				while ('0' <= cat[i] && cat[i] <= '9') { num2 *= 10; num2 = cat[i] - '0'; i++; }
			}

			switch (cat[i]) {
				case 's': {
					nhstr s = va_arg(the_args, nhstr);
					nhscat(str, s);
					if (left_justify && num1 > (int)s.len) {
						for (int i = 0; i < ((int)s.len) - num1; i++) nhscatz(str, " ");
					}

					if (should_destroy) del_nhs(&s);
					break;
				}
				case 'S': {
					char *s = va_arg(the_args, char*);
					nhscatzc(str, s, style);
					if (should_destroy) free(s);
					break;
				}
				case 'c':
					nhscatzc(str, utf8_tmpstr(va_arg(the_args, glyph_t)), style);
					break;
				case 'l': {
					static char buf[BUFSZ];
					if (num1 != -1) {
						if (num2 != -1) sprintf(buf, "%*.*ld", num1, num2, va_arg(the_args, long));
						else sprintf(buf, "%*ld", num1, va_arg(the_args, long));
					} else {
						sprintf(buf, "%ld", va_arg(the_args, long));
					}

					nhscatzc(str, buf, style);
					break;
				}
				case 'i': {
					static char buf[BUFSZ];
					if (num1 != -1) {
						if (num2 != -1) sprintf(buf, "%*.*i", num1, num2, va_arg(the_args, int));
						else sprintf(buf, "%*i", num1, va_arg(the_args, int));
					} else {
						sprintf(buf, "%i", va_arg(the_args, int));
					}

					nhscatzc(str, buf, style);
					break;
				}
				case 'u': {
					static char buf[BUFSZ];
					if (num1 != -1) {
						if (num2 != -1) sprintf(buf, "%*.*u", num1, num2, va_arg(the_args, uint));
						else sprintf(buf, "%*u", num1, va_arg(the_args, uint));
					} else {
						sprintf(buf, "%u", va_arg(the_args, uint));
					}

					nhscatzc(str, buf, style);
					break;
				}
				case '%':
					nhscatzc(str, "%", style);
					break;
				// bad format
				default:
					impossible("bad string format '%c'", cat[i]);
					nhscatzc(str, "%", style);
					nhscatznc(str, cat + i, 1, style);
			}
		} else {
			nhscatznc(str, cat + i, 1, style);
		}
	}

	return str;
}

nhstr *nhscatfc(nhstr *str, nhstyle style, const char *cat, ...) {
	VA_START(cat);
	nhstr *ret = nhscatfc_v(str, style, cat, VA_ARGS);
	VA_END();

	return ret;
}

nhstr *nhscatf(nhstr *str, const char *cat, ...) {
	VA_START(cat);
	nhstr *ret = nhscatfc_v(str, nhstyle_default(), cat, VA_ARGS);
	VA_END();

	return ret;
}

nhstr *nhsmove(nhstr *target, nhstr *src) {
	if (!memcmp(src, target, sizeof(nhstr))) {
		return target;
	}

	free(target->str);
	target->str = src->str;
	src->str = NULL;

	free(target->style);
	target->style = src->style;
	src->style = NULL;

	target->len = src->len;
	src->len = 0;

	return target;
}

nhstr *nhscopyf(nhstr *target, const char *fmt, ...) {
	nhstr tmp = {0};
	VA_START(fmt);
	nhsmove(target, nhscatfc_v(&tmp, nhstyle_default(), fmt, VA_ARGS));
	VA_END();
	return target;
}

nhstr *nhscopyz(nhstr *str, const char *replacement) {
	nhstr tmp = {0};
	return nhsmove(str, nhscatz(&tmp, replacement));
}

nhstr *nhsreplace(nhstr *str, const nhstr replacement) {
	nhstr tmp = nhsdup(replacement);
	return nhsmove(str, &tmp);
}

nhstr nhsfmt(const char *fmt, ...) {
	nhstr tmp = {0};
	VA_START(fmt);
	nhscatfc_v(&tmp, nhstyle_default(), fmt, VA_ARGS);
	VA_END();
	return tmp;
}

usize utf8len(const nhstr str) {
	usize ret = 0;
	for (usize i = 0; i < str.len; i++) {
		ret += strlen(utf8_tmpstr(str.str[i]));
	}

	return ret;
}

nhstr *nhstmp(nhstr *str) {
	static nhstr store[NHSTMP_BACKING_STORE_SIZE];
	static usize index;

	nhstr *ret = &store[index];
	index = (index + 1) % NHSTMP_BACKING_STORE_SIZE;
	return nhsmove(ret, str);
}

nhstr *nhstmpt(nhstr str) { return nhstmp(&str); }

// does utf-8 conversion
char *nhs2cstr_tmp(const nhstr str) {
	static char *ret = NULL;
	static usize retlen = 0;

	if (retlen <= utf8len(str)) {
		ret = realloc(ret, retlen = (utf8len(str)+1));
	}

	ret[0] = 0;
	for (usize i = 0; i < str.len; i++) {
		strcat(ret, utf8_tmpstr(str.str[i]));
	}

	return ret;
}

// doesn't do unicode conversion
char *nhs2cstr_trunc_tmp(const nhstr str) {
	static char *ret = NULL;
	static usize retlen = 0;

	if (retlen <= str.len) {
		ret = realloc(ret, retlen = (str.len+1));
	}

	usize i;
	for (i = 0; i < str.len; i++) {
		ret[i] = str.str[i];
	}
	ret[str.len] = 0;

	return ret;
}

char *nhs2cstr_tmp_destroy(nhstr *str) {
	char *ret = nhs2cstr_tmp(*str);
	del_nhs(str);
	return ret;
}

nhstr *nhstrim(nhstr *str, usize maxlen) {
	if (maxlen < str->len) {
		str->len = maxlen;
		str->str = realloc(str->str, str->len * sizeof(glyph_t));
		str->style = realloc(str->str, str->len * sizeof(nhstyle));
	}

	return str;
}

nhstr *nhslice(nhstr *str, usize new_start) {
	if (new_start < str->len) {
		memmove(str->str, (str->str + new_start), (str->len - new_start) * sizeof(glyph_t));
		str->str = realloc(str->str, (str->len - new_start) * sizeof(glyph_t));

		memmove(str->style, str->style + new_start, (str->len - new_start) * sizeof(nhstyle));
		str->style = realloc(str->style, (str->len - new_start) * sizeof(nhstyle));

		str->len -= new_start;
	}

	return str;
}

isize nhsindex(const nhstr str, glyph_t ch) {
	for (usize i = 0; i < str.len; i++) {
		if (str.str[i] == ch) {
			return i;
		}
	}

	return -1;  // no match
}

void save_nhs(int fd, const nhstr str) {
	usize len = str.len; // silence warning
	bwrite(fd, &len, sizeof(usize));

	if (len) {
		bwrite(fd, str.str, str.len * sizeof(glyph_t));
		bwrite(fd, str.style, str.len * sizeof(nhstyle));
	}
}

nhstr restore_nhs(int fd) {
	nhstr ret;

	mread(fd, &ret.len, sizeof(usize));

	// note: intentional alloc(0)
	ret.str = new(glyph_t, ret.len);
	ret.style = new(nhstyle, ret.len);

	if (ret.len) {
		mread(fd, ret.str, ret.len * sizeof(glyph_t));
		mread(fd, ret.style, ret.len * sizeof(nhstyle));
	}

	return ret;
}
