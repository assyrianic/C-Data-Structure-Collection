#ifdef OS_WINDOWS
#	define HARBOL_LIB
#endif

#include "harbol.h"

/* CFG Parser in EBNF grammar
	keyval = <string> [':'] (<value>|<section>) [','] ;
	section = '{' <keyval> '}' ;
	value = <string> | <number> | <color> | "true" | "false" | "null" ;
	matrix = '[' <number> [','] [<number>] [','] [<number>] [','] [<number>] ']' ;
	color = 'c' <matrix> ;
	vecf = 'v' <matrix> ;
	string = '"' chars '"' | "'" chars "'" ;
*/

#define HARBOL_CFG_ERR_STK_SIZE 20

static struct {
	struct HarbolString errs[HARBOL_CFG_ERR_STK_SIZE];
	size_t count, curr_line;
} _g_cfg_err;

static bool is_decimal(const char c)
{
	return( c >= '0' && c <= '9' );
}

static bool is_octal(const char c)
{
	return( c >= '0' && c <= '7' );
}

static bool is_hex(const char c)
{
	return( (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || is_decimal(c) );
}

static bool is_whitespace(const char c)
{
	return( c==' ' || c=='\t' || c=='\r' || c=='\v' || c=='\f' || c=='\n' );
}


static void skip_single_comment(const char **strref)
{
	if( !*strref || !**strref )
		return;
	else for(; **strref != '\n'; (*strref)++ );
}

static void skip_multiline_comment(const char **strref)
{
	if( !*strref || !**strref )
		return;
	// skip past '/' && '*'
	*strref += 2;
	do {
		if( !**strref || !(*strref)[1] )
			break;
		if( **strref=='\n' )
			_g_cfg_err.curr_line++;
		(*strref)++;
	} while( !(**strref=='*' && (*strref)[1]=='/') );
	if( **strref && (*strref)[1] )
		*strref += 2;
}

static bool skip_whitespace(const char **strref)
{
	if( !*strref || !**strref )
		return false;
	
	while( **strref && is_whitespace(**strref) ) {
		if( **strref=='\n' )
			_g_cfg_err.curr_line++;
		(*strref)++;
	}
	return **strref != 0;
}

static bool skip_ws_and_comments(const char **strref)
{
	if( !*strref || !**strref ) {
		return false;
	} else {
		while( **strref && (is_whitespace(**strref) || // white space
				**strref=='#' || (**strref=='/' && (*strref)[1]=='/') || // single line comment
				(**strref=='/' && (*strref)[1]=='*') || // multi-line comment
				**strref==':' || **strref==',') ) // delimiters.
		{
			if( is_whitespace(**strref) )
				skip_whitespace(strref);
			else if( **strref=='#' || (**strref=='/' && (*strref)[1]=='/') ) {
				skip_single_comment(strref);
				_g_cfg_err.curr_line++;
			} else if( **strref=='/' && (*strref)[1]=='*' )
				skip_multiline_comment(strref);
			else if( **strref==':' || **strref==',' )
				(*strref)++;
		}
		return **strref != 0;
	}
}

static int32_t _lex_hex_escape_char(const char **restrict strref)
{
	int32_t r = 0;
	if( !is_hex(**strref) ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: \\x escape hex with no digits! '%c'. Line: %zu\n", **strref, _g_cfg_err.curr_line);
	} else {
		for(; **strref; (*strref)++ ) {
			const char c = **strref;
			if( c>='0' && c<='9' )
				r = (r << 4) | (c - '0');
			else if( c>='a' && c<='f' )
				r = (r << 4) | (c - 'a' + 10);
			else if( c>='A' && c<='F' )
				r = (r << 4) | (c - 'A' + 10);
			else return r;
		}
	}
	return r;
}

static bool _lex_string(const char **restrict strref, struct HarbolString *const restrict str)
{
	if( !*strref || !**strref || !str )
		return false;
	else if( !(**strref == '"' || **strref == '\'') ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid string quote mark: '%c'. Line: %zu\n", **strref, _g_cfg_err.curr_line);
		return false;
	}
	const char quote = *(*strref)++;
	while( **strref && **strref != quote ) {
		const char chrval = *(*strref)++;
		if( chrval=='\\' ) {
			const char chr = *(*strref)++;
			switch( chr ) {
				case 'a': harbol_string_add_char(str, '\a'); break;
				case 'r': harbol_string_add_char(str, '\r'); break;
				case 'b': harbol_string_add_char(str, '\b'); break;
				case 't': harbol_string_add_char(str, '\t'); break;
				case 'v': harbol_string_add_char(str, '\v'); break;
				case 'n': harbol_string_add_char(str, '\n'); break;
				case 'f': harbol_string_add_char(str, '\f'); break;
				case 's': harbol_string_add_char(str, ' '); break;
				case 'x': harbol_string_add_char(str, (char)_lex_hex_escape_char(strref)); break;
				default: harbol_string_add_char(str, chr);
			}
		}
		else harbol_string_add_char(str, chrval);
	}
	if( **strref==quote )
		(*strref)++;
	
	// Patch, if an empty string was given, we allocate an empty string for the string.
	if( str->CStr==NULL )
		harbol_string_copy_cstr(str, "");
	return **strref != 0;
}

static bool _lex_number(const char **restrict strref, struct HarbolString *const restrict str, enum HarbolCfgType *const typeref)
{
	if( !*strref || !**strref )
		return false;
	
	if( **strref=='-' || **strref=='+' )
		harbol_string_add_char(str, *(*strref)++);
	
	if( !is_decimal(**strref) && **strref!='.' ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid initial numeric digit: '%c'. Line: %zu\n", **strref, _g_cfg_err.curr_line);
		return false;
	} else if( **strref=='0' ) {
		harbol_string_add_char(str, *(*strref)++);
		const char numtype = *(*strref)++;
		harbol_string_add_char(str, numtype);
		*typeref = HarbolTypeInt;
		switch( numtype ) {
			case 'X': case 'x': // hex
				harbol_string_add_char(str, *(*strref)++);
				while( is_hex(**strref) )
					harbol_string_add_char(str, *(*strref)++);
				break;
			case '.': // float
				*typeref = HarbolTypeFloat;
				harbol_string_add_char(str, *(*strref)++);
				while( is_decimal(**strref) || **strref=='e' || **strref=='E' || **strref=='f' || **strref=='F' )
					harbol_string_add_char(str, *(*strref)++);
				break;
			default: // octal
				while( is_octal(**strref) )
					harbol_string_add_char(str, *(*strref)++);
		}
	}
	else if( is_decimal(**strref) ) { // numeric value. Check if float possibly.
		*typeref = HarbolTypeInt;
		while( is_decimal(**strref) )
			harbol_string_add_char(str, *(*strref)++);
		
		if( **strref=='.' ) { // definitely float value.
			*typeref = HarbolTypeFloat;
			harbol_string_add_char(str, *(*strref)++);
			while( is_decimal(**strref) || **strref=='e' || **strref=='E' || **strref=='f' || **strref=='F' )
				harbol_string_add_char(str, *(*strref)++);
		}
	}
	else if( **strref=='.' ) { // float value.
		*typeref = HarbolTypeFloat;
		harbol_string_add_char(str, *(*strref)++);
		while( is_decimal(**strref) || **strref=='e' || **strref=='E' )
			harbol_string_add_char(str, *(*strref)++);
	}
	return str->Len > 0;
}

static bool harbol_cfg_parse_section(struct HarbolLinkMap *, const char **);
static bool harbol_cfg_parse_number(struct HarbolLinkMap *, const struct HarbolString *, const char **);

// keyval = <string> [':'] (<value>|<section>) [','] ;
static bool harbol_cfg_parse_key_val(struct HarbolLinkMap *const restrict map, const char **cfgcoderef)
{
	if( !map ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid harbol_linkmap ptr!\n");
		return false;
	} else if( !*cfgcoderef ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid config buffer!\n");
		return false;
	} else if( !**cfgcoderef || !skip_ws_and_comments(cfgcoderef) )
		return false;
	else if( **cfgcoderef!='"' && **cfgcoderef!='\'' ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: missing beginning quote for key '%c'. Line: %zu\n", **cfgcoderef, _g_cfg_err.curr_line);
		return false;
	}
	
	struct HarbolString keystr = {NULL, 0};
	const bool strresult = _lex_string(cfgcoderef, &keystr);
	if( !strresult ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid string key '%s'. Line: %zu\n", keystr.CStr, _g_cfg_err.curr_line);
		harbol_string_del(&keystr);
		return false;
	} else if( harbol_linkmap_has_key(map, keystr.CStr) ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: duplicate string key '%s'. Line: %zu\n", keystr.CStr, _g_cfg_err.curr_line);
		harbol_string_del(&keystr);
		return false;
	}
	skip_ws_and_comments(cfgcoderef);
	
	bool res = false;
	// it's a section!
	if( **cfgcoderef=='{' ) {
		struct HarbolLinkMap *restrict subsection = harbol_linkmap_new();
		res = harbol_cfg_parse_section(subsection, cfgcoderef);
		struct HarbolVariant *var = harbol_variant_new((union HarbolValue){.Ptr=subsection}, HarbolTypeLinkMap);
		const bool inserted = harbol_linkmap_insert(map, keystr.CStr, (union HarbolValue){ .VarPtr=var });
		if( !inserted )
			harbol_variant_free(&var, (fnHarbolDestructor *)harbol_cfg_free);
	}
	// string value.
	else if( **cfgcoderef=='"'||**cfgcoderef=='\'' ) {
		struct HarbolString *str = harbol_string_new();
		res = _lex_string(cfgcoderef, str);
		if( !res ) {
			if( !str ) {
				if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
					harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: unable to allocate string value. Line: %zu\n", _g_cfg_err.curr_line);
			} else {
				if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
					harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid string value '%s'. Line: %zu\n", str->CStr, _g_cfg_err.curr_line);
			} return false;
		}
		struct HarbolVariant *var = harbol_variant_new((union HarbolValue){.Ptr=str}, HarbolTypeString);
		harbol_linkmap_insert(map, keystr.CStr, (union HarbolValue){ .VarPtr=var });
	}
	// color/vector value!
	else if( **cfgcoderef=='c' || **cfgcoderef=='v' ) {
		const char valtype = *(*cfgcoderef)++;
		skip_ws_and_comments(cfgcoderef);
		
		if( **cfgcoderef!='[' ) {
			if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
				harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: missing '[' '%c'. Line: %zu\n", **cfgcoderef, _g_cfg_err.curr_line);
			harbol_string_del(&keystr);
			return false;
		}
		(*cfgcoderef)++;
		skip_ws_and_comments(cfgcoderef);
		
		union {
			union HarbolColor color;
			union HarbolVec4D vec4d;
		} matrix_value = {0};
		
		size_t iterations = 0;
		while( **cfgcoderef && **cfgcoderef != ']' ) {
			struct HarbolString numstr = {NULL, 0};
			enum HarbolCfgType type = HarbolTypeNull;
			const bool result = _lex_number(cfgcoderef, &numstr, &type);
			if( iterations<4 ) {
				if( valtype=='c' ) {
					matrix_value.color.RGBA[iterations++] = (uint8_t)strtoul(numstr.CStr, NULL, 0);
				}
				else {
					matrix_value.vec4d.XYZW[iterations++] = (float)strtof(numstr.CStr, NULL);
				}
			}
			harbol_string_del(&numstr);
			if( !result ) {
				if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
					harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid number in [] array. Line: %zu\n", _g_cfg_err.curr_line);
				harbol_string_del(&keystr);
				return false;
			}
			skip_ws_and_comments(cfgcoderef);
		}
		if( !**cfgcoderef ) {
			if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
				harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: unexpected end of file with missing ending ']'. Line: %zu\n", _g_cfg_err.curr_line);
			return false;
		}
		(*cfgcoderef)++;
		
		const size_t matrix_data[] = { valtype=='c' ? sizeof(union HarbolColor) : sizeof(union HarbolVec4D) };
		struct HarbolTuple *tuple = harbol_tuple_new(1, matrix_data, false);
		valtype=='c' ?
			harbol_tuple_set_field(tuple, 0, &matrix_value.color.UIntColor)
			: harbol_tuple_set_field(tuple, 0, &matrix_value.vec4d.XYZW[0]);
		
		struct HarbolVariant *var = harbol_variant_new((union HarbolValue){.TuplePtr=tuple}, valtype=='c' ? HarbolTypeColor : HarbolTypeVec4D);
		res = harbol_linkmap_insert(map, keystr.CStr, (union HarbolValue){ .VarPtr=var });
	}
	// true bool value.
	else if( **cfgcoderef=='t' ) {
		if( strncmp("true", *cfgcoderef, sizeof("true")-1) ) {
			if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
				harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid word value, only 'true', 'false' or 'null' are allowed. Line: %zu\n", _g_cfg_err.curr_line);
			harbol_string_del(&keystr);
			return false;
		}
		*cfgcoderef += sizeof("true") - 1;
		struct HarbolVariant *var = harbol_variant_new((union HarbolValue){.Bool=true}, HarbolTypeBool);
		res = harbol_linkmap_insert(map, keystr.CStr, (union HarbolValue){ .VarPtr=var });
	}
	// false bool value.
	else if( **cfgcoderef=='f' ) {
		if( strncmp("false", *cfgcoderef, sizeof("false")-1) ) {
			if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
				harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid word value, only 'true', 'false' or 'null' are allowed. Line: %zu\n", _g_cfg_err.curr_line);
			harbol_string_del(&keystr);
			return false;
		}
		*cfgcoderef += sizeof("false") - 1;
		struct HarbolVariant *var = harbol_variant_new((union HarbolValue){.Bool=false}, HarbolTypeBool);
		res = harbol_linkmap_insert(map, keystr.CStr, (union HarbolValue){ .VarPtr=var });
	}
	// null value.
	else if( **cfgcoderef=='n' ) {
		if( strncmp("null", *cfgcoderef, sizeof("null")-1) ) {
			if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
				harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid word value, only 'true', 'false' or 'null' are allowed. Line: %zu\n", _g_cfg_err.curr_line);
			harbol_string_del(&keystr);
			return false;
		}
		*cfgcoderef += sizeof("null") - 1;
		struct HarbolVariant *var = harbol_variant_new((union HarbolValue){0}, HarbolTypeNull);
		res = harbol_linkmap_insert(map, keystr.CStr, (union HarbolValue){ .VarPtr=var });
	}
	// numeric value.
	else if( is_decimal(**cfgcoderef) || **cfgcoderef=='.' || **cfgcoderef=='-' || **cfgcoderef=='+' ) {
		res = harbol_cfg_parse_number(map, &keystr, cfgcoderef);
	} else if( **cfgcoderef=='[' ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: array bracket missing 'c' or 'v' tag. Line: %zu\n", _g_cfg_err.curr_line);
		harbol_string_del(&keystr);
		return false;
	} else {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: unknown character detected '%c'. Line: %zu\n", **cfgcoderef, _g_cfg_err.curr_line);
		res = false;
	}
	harbol_string_del(&keystr);
	skip_ws_and_comments(cfgcoderef);
	return res;
}

static bool harbol_cfg_parse_number(struct HarbolLinkMap *const restrict map, const struct HarbolString *const restrict key, const char **cfgcoderef)
{
	struct HarbolString numstr = {NULL, 0};
	enum HarbolCfgType type = HarbolTypeNull;
	const bool result = _lex_number(cfgcoderef, &numstr, &type);
	if( !result ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: invalid number. Line: %zu\n", _g_cfg_err.curr_line);
		harbol_string_del(&numstr);
		return result;
	} else {
		struct HarbolVariant *var = harbol_variant_new(type==HarbolTypeFloat ? (union HarbolValue){.Double=strtod(numstr.CStr, NULL)} : (union HarbolValue){.Int64=strtoll(numstr.CStr, NULL, 0)}, type);
		harbol_string_del(&numstr);
		return harbol_linkmap_insert(map, key->CStr, (union HarbolValue){ .VarPtr=var });
	}
}

// section = '{' <keyval> '}' ;
static bool harbol_cfg_parse_section(struct HarbolLinkMap *const restrict map, const char **cfgcoderef)
{
	if( **cfgcoderef!='{' ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: missing '{' '%c' for section. Line: %zu\n", **cfgcoderef, _g_cfg_err.curr_line);
		return false;
	}
	(*cfgcoderef)++;
	skip_ws_and_comments(cfgcoderef);
	
	while( **cfgcoderef && **cfgcoderef != '}' ) {
		const bool res = harbol_cfg_parse_key_val(map, cfgcoderef);
		if( !res )
			return false;
	}
	if( !**cfgcoderef ) {
		if( _g_cfg_err.count < HARBOL_CFG_ERR_STK_SIZE )
			harbol_string_format(&_g_cfg_err.errs[_g_cfg_err.count++], "Harbol Config Parser :: unexpected end of file with missing '}' for section. Line: %zu\n", _g_cfg_err.curr_line);
		return false;
	}
	(*cfgcoderef)++;
	return true;
}


HARBOL_EXPORT struct HarbolLinkMap *harbol_cfg_from_file(const char filename[restrict])
{
	if( !filename )
		return NULL;
	
	FILE *restrict cfgfile = fopen(filename, "r");
	if( !cfgfile ) {
		fprintf(stderr, "harbol_cfg_from_file :: unable to find file '%s'.\n", filename);
		return NULL;
	}
	fseek(cfgfile, 0, SEEK_END);
	const long filesize = ftell(cfgfile);
	if( filesize <= -1 ) {
		fprintf(stderr, "harbol_cfg_from_file :: size of file '%s' is negative: '%li'\n", filename, filesize);
		fclose(cfgfile), cfgfile=NULL;
		return NULL;
	}
	rewind(cfgfile);
	
	char *restrict cfgcode = calloc(filesize+1, sizeof *cfgcode);
	if( !cfgcode ) {
		fprintf(stderr, "harbol_cfg_from_file :: unable to allocate buffer for file '%s'.\n", filename);
		fclose(cfgfile), cfgfile=NULL;
		return NULL;
	}
	const size_t val = fread(cfgcode, sizeof *cfgcode, filesize, cfgfile);
	fclose(cfgfile), cfgfile=NULL;
	if( val != (size_t)filesize ) {
		fprintf(stderr, "harbol_cfg_from_file :: filesize '%li' does not match fread return value '%zu' for file '%s'\n", filesize, val, filename);
		free(cfgcode), cfgcode=NULL;
		return NULL;
	}
	struct HarbolLinkMap *const restrict objs = harbol_cfg_parse_cstr(cfgcode);
	free(cfgcode), cfgcode=NULL;
	return objs;
}


HARBOL_EXPORT struct HarbolLinkMap *harbol_cfg_parse_cstr(const char cfgcode[])
{
	if( !cfgcode )
		return NULL;
	else {
		_g_cfg_err.curr_line = 1;
		const char *iter = cfgcode;
		struct HarbolLinkMap *objs = harbol_linkmap_new();
		while( harbol_cfg_parse_key_val(objs, &iter) );
		if( _g_cfg_err.count > 0 ) {
			for( size_t i=0; i<_g_cfg_err.count; i++ ) {
				fputs(_g_cfg_err.errs[i].CStr, stderr);
				harbol_string_del(&_g_cfg_err.errs[i]);
			}
			memset(&_g_cfg_err, 0, sizeof _g_cfg_err);
		}
		return objs;
	}
}

static void _harbol_cfgkey_del(struct HarbolVariant *const var)
{
	switch( var->TypeTag ) {
		case HarbolTypeLinkMap:
			harbol_cfg_free(&var->Val.LinkMapPtr); break;
		case HarbolTypeString:
			harbol_string_free(&var->Val.StrObjPtr); break;
		case HarbolTypeColor:
		case HarbolTypeVec4D:
			harbol_tuple_free(&var->Val.TuplePtr); break;
	}
	memset(var, 0, sizeof *var);
}

HARBOL_EXPORT bool harbol_cfg_free(struct HarbolLinkMap **mapref)
{
	if( !mapref || !*mapref )
		return false;
	else {
		const union HarbolValue *const end = harbol_linkmap_get_iter_end_count(*mapref);
		for( union HarbolValue *iter = harbol_linkmap_get_iter(*mapref); iter && iter<end; iter++ ) {
			struct HarbolKeyValPair *n = iter->KvPairPtr;
			_harbol_cfgkey_del(n->Data.VarPtr);
			harbol_variant_free(&n->Data.VarPtr, NULL);
		}
		harbol_linkmap_free(mapref, NULL);
		return *mapref==NULL;
	}
}

/*
static const char *Harbol_GetTypeName(const enum HarbolCfgType type)
{
	switch( type ) {
		case HarbolTypeNull:        return "Null";
		case HarbolTypeLinkMap:     return "LinkMap";
		case HarbolTypeString:      return "String";
		case HarbolTypeFloat:       return "Float";
		case HarbolTypeInt:         return "Int";
		case HarbolTypeBool:        return "Boolean";
		case HarbolTypeColor:       return "Color";
		case HarbolTypeVec4D:       return "Vector";
		default: return "Unknown Type";
	}
}
*/

HARBOL_EXPORT bool harbol_cfg_to_str(const struct HarbolLinkMap *const restrict map, struct HarbolString *const str)
{
	if( !map || !str )
		return false;
	else {
		# define BUFFER_SIZE    512
		const union HarbolValue *const end = harbol_linkmap_get_iter_end_count(map);
		for( union HarbolValue *iter = harbol_linkmap_get_iter(map); iter && iter<end; iter++ ) {
			const struct HarbolKeyValPair *kv = iter->KvPairPtr;
			const int32_t type = kv->Data.VarPtr->TypeTag;
			// print out key and notation.
			harbol_string_add_char(str, '"');
			harbol_string_add_str(str, &kv->KeyName);
			harbol_string_add_cstr(str, "\": ");
		
			char buffer[BUFFER_SIZE] = {0};
			switch( type ) {
				case HarbolTypeNull:
					harbol_string_add_cstr(str, "null\n");
					break;
				case HarbolTypeLinkMap:
					harbol_string_add_cstr(str, "{\n");
					harbol_cfg_to_str(kv->Data.VarPtr->Val.LinkMapPtr, str);
					harbol_string_add_cstr(str, "}\n");
					break;
				case HarbolTypeString:
					harbol_string_add_cstr(str, "\"");
					harbol_string_add_str(str, kv->Data.VarPtr->Val.StrObjPtr);
					harbol_string_add_cstr(str, "\"\n");
					break;
				case HarbolTypeFloat:
					snprintf(buffer, BUFFER_SIZE, "%f\n", kv->Data.VarPtr->Val.Double);
					harbol_string_add_cstr(str, buffer);
					break;
				case HarbolTypeInt:
					snprintf(buffer, BUFFER_SIZE, "%" PRIi64 "\n", kv->Data.VarPtr->Val.Int64);
					harbol_string_add_cstr(str, buffer);
					break;
				case HarbolTypeBool:
					harbol_string_add_cstr(str, kv->Data.VarPtr->Val.Bool ? "true\n" : "false\n");
					break;
				case HarbolTypeColor: {
					harbol_string_add_cstr(str, "c[ ");
					struct { uint8_t r,g,b,a; } color = {0};
					harbol_tuple_to_struct(kv->Data.VarPtr->Val.TuplePtr, &color);
					snprintf(buffer, BUFFER_SIZE, "%u, %u, %u, %u ]\n", color.r, color.g, color.b, color.a);
					harbol_string_add_cstr(str, buffer);
					break;
				}
				case HarbolTypeVec4D: {
					harbol_string_add_cstr(str, "v[ ");
					struct { float x,y,z,w; } vec4 = {0};
					harbol_tuple_to_struct(kv->Data.VarPtr->Val.TuplePtr, &vec4);
					snprintf(buffer, BUFFER_SIZE, "%f, %f, %f, %f ]\n", vec4.x, vec4.y, vec4.z, vec4.w);
					harbol_string_add_cstr(str, buffer);
					break;
				}
			}
		}
		return str->Len > 0;
	}
}

static bool harbol_cfg_parse_target_path(const char key[], struct HarbolString *const restrict str)
{
	if( !key || !str )
		return false;
	else {
		// parse something like: "root.section1.section2.section3./.dotsection"
		const char *iter = key;
		/*
			iterate to the null terminator and then work backwards to the last dot.
			ughhh too many while loops lmao.
		*/
		iter += strlen(key) - 1;
		while( iter != key ) {
			// Patch: allow keys to use dot without interfering with dot path.
			// check if we hit a dot.
			if( *iter=='.' ) {
				// if we hit a dot, check if the previous char is an "escape" char.
				if( iter[-1]=='/' || iter[-1]=='\\' ) {
					iter--;
				} else {
					iter++;
					break;
				}
			} else {
				iter--;
			}
		}
		// now we save the target section and then use the resulting string.
		while( *iter ) {
			if( *iter=='/' ) {
				iter++;
				continue;
			} else harbol_string_add_char(str, *iter++);
		}
		return str->Len > 0;
	}
}

static struct HarbolVariant *_get_var_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[])
{
	/* first check if we're getting a singular value OR we iterate through a sectional path. */
	const char *dot = strchr(key, '.');
	// Patch: dot and escaped dot glitching out the hashmap hashing...
	if( !dot || (dot>key && (dot[-1] == '/' || dot[-1] == '\\')) ) {
		struct HarbolVariant *restrict var = harbol_linkmap_get(cfgmap, key).VarPtr;
		return ( !var || var->TypeTag==HarbolTypeNull ) ? NULL : var;
	}
	/* ok, not a singular value, iterate to the specific linkmap section then. */
	else {
		// parse the target key first.
		const char *iter = key;
		struct HarbolString
			sectionstr = {NULL, 0},
			targetstr = {NULL, 0}
		;
		harbol_cfg_parse_target_path(key, &targetstr);
		struct HarbolLinkMap *itermap = cfgmap;
		struct HarbolVariant *restrict var = NULL;
	
		while( itermap ) {
			harbol_string_del(&sectionstr);
			// Patch: allow keys to use dot without interfering with dot path.
			while( *iter ) {
				if( (*iter=='/' || *iter=='\\') && iter[1] && iter[1]=='.' ) {
					iter++;
					harbol_string_add_char(&sectionstr, *iter++);
				} else if( *iter=='.' ) {
					iter++;
					break;
				} else {
					harbol_string_add_char(&sectionstr, *iter++);
				}
			}
			var = harbol_linkmap_get(itermap, sectionstr.CStr).VarPtr;
			if( !var || !harbol_string_cmpstr(&sectionstr, &targetstr) )
				break;
			else if( var->TypeTag==HarbolTypeLinkMap )
				itermap = var->Val.LinkMapPtr;
		}
		harbol_string_del(&sectionstr);
		harbol_string_del(&targetstr);
		return var;
	}
}

HARBOL_EXPORT struct HarbolLinkMap *harbol_cfg_get_section_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[])
{
	if( !cfgmap || !key )
		return NULL;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		return !var || var->TypeTag != HarbolTypeLinkMap ? NULL : var->Val.LinkMapPtr;
	}
}

HARBOL_EXPORT char *harbol_cfg_get_str_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[])
{
	if( !cfgmap || !key )
		return NULL;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		return !var || var->TypeTag != HarbolTypeString ? NULL : var->Val.StrObjPtr->CStr;
	}
}

HARBOL_EXPORT bool harbol_cfg_get_float_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], double *const restrict dblref)
{
	if( !cfgmap || !key || !dblref )
		return false;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var || var->TypeTag != HarbolTypeFloat ) {
			return false;
		} else {
			*dblref = var->Val.Double;
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_get_int_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], int64_t *const restrict i64ref)
{
	if( !cfgmap || !key || !i64ref )
		return false;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var || var->TypeTag != HarbolTypeInt ) {
			return false;
		} else {
			*i64ref = var->Val.Int64;
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_get_bool_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], bool *const restrict boolref)
{
	if( !cfgmap || !key || !boolref )
		return false;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var || var->TypeTag != HarbolTypeBool ) {
			return false;
		} else {
			*boolref = var->Val.Bool;
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_get_color_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], union HarbolColor *const restrict colorref)
{
	if( !cfgmap || !key || !colorref )
		return false;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		return ( !var || var->TypeTag != HarbolTypeColor ) ? false : harbol_tuple_to_struct(var->Val.TuplePtr, colorref);
	}
}


HARBOL_EXPORT bool harbol_cfg_get_vec4D_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], union HarbolVec4D *const restrict vec4ref)
{
	if( !cfgmap || !key || !vec4ref )
		return false;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		return ( !var || var->TypeTag != HarbolTypeVec4D ) ? false : harbol_tuple_to_struct(var->Val.TuplePtr, vec4ref);
	}
}

HARBOL_EXPORT bool harbol_cfg_get_key_type(struct HarbolLinkMap *const restrict cfgmap, const char key[], enum HarbolCfgType *const restrict type)
{
	if( !cfgmap || !key )
		return false;
	else {
		const struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var )
			return false;
		else if( type )
			*type = var->TypeTag;
		return true;
	}
}


HARBOL_EXPORT bool harbol_cfg_set_str_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], const char cstr[restrict], const bool override_convert)
{
	if( !cfgmap || !key || !cstr )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var )
			return false;
		else if( var->TypeTag != HarbolTypeString ) {
			if( override_convert ) {
				_harbol_cfgkey_del(var);
				var->TypeTag = HarbolTypeString;
				var->Val.StrObjPtr = harbol_string_new_cstr(cstr);
				return true;
			}
			else return false;
		} else {
			harbol_string_copy_cstr(var->Val.StrObjPtr, cstr);
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_set_float_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], const double val, const bool override_convert)
{
	if( !cfgmap || !key )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var )
			return false;
		else if( var->TypeTag != HarbolTypeFloat ) {
			if( override_convert ) {
				_harbol_cfgkey_del(var);
				var->TypeTag = HarbolTypeFloat;
				var->Val.Double = val;
				return true;
			}
			else return false;
		} else {
			var->Val.Double = val;
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_set_int_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], const int64_t val, const bool override_convert)
{
	if( !cfgmap || !key )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var )
			return false;
		else if( var->TypeTag != HarbolTypeInt ) {
			if( override_convert ) {
				_harbol_cfgkey_del(var);
				var->TypeTag = HarbolTypeInt;
				var->Val.Int64 = val;
				return true;
			}
			else return false;
		} else {
			var->Val.Int64 = val;
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_set_bool_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], const bool val, const bool override_convert)
{
	if( !cfgmap || !key )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var )
			return false;
		else if( var->TypeTag != HarbolTypeBool ) {
			if( override_convert ) {
				_harbol_cfgkey_del(var);
				var->TypeTag = HarbolTypeBool;
				var->Val.Bool = val;
				return true;
			}
			else return false;
		} else {
			var->Val.Bool = val;
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_set_color_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], const union HarbolColor *const restrict val, const bool override_convert)
{
	if( !cfgmap || !key || !val )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var )
			return false;
		else if( var->TypeTag != HarbolTypeColor ) {
			if( override_convert ) {
				_harbol_cfgkey_del(var);
				var->TypeTag = HarbolTypeColor;
				const size_t matrix_data[] = { sizeof(union HarbolColor) };
				var->Val.TuplePtr = harbol_tuple_new(1, matrix_data, false);
				memcpy(var->Val.TuplePtr, val, sizeof *val);
				return true;
			}
			else return false;
		} else {
			memcpy(var->Val.TuplePtr, val, sizeof *val);
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_set_vec4D_by_key(struct HarbolLinkMap *const restrict cfgmap, const char key[], const union HarbolVec4D *const restrict val, const bool override_convert)
{
	if( !cfgmap || !key || !val )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var ) {
			return false;
		} else if( var->TypeTag != HarbolTypeVec4D ) {
			if( override_convert ) {
				_harbol_cfgkey_del(var);
				var->TypeTag = HarbolTypeVec4D;
				const size_t matrix_data[] = { sizeof(union HarbolVec4D) };
				var->Val.TuplePtr = harbol_tuple_new(1, matrix_data, false);
				memcpy(var->Val.TuplePtr, val, sizeof *val);
				return true;
			}
			else return false;
		} else {
			memcpy(var->Val.TuplePtr, val, sizeof *val);
			return true;
		}
	}
}

HARBOL_EXPORT bool harbol_cfg_set_key_to_null(struct HarbolLinkMap *const restrict cfgmap, const char key[])
{
	if( !cfgmap || !key )
		return false;
	else {
		struct HarbolVariant *restrict var = _get_var_by_key(cfgmap, key);
		if( !var ) {
			return false;
		} else {
			_harbol_cfgkey_del(var);
			var->TypeTag = HarbolTypeNull;
			return true;
		}
	}
}

/*
HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_section(void)
{
	struct HarbolLinkMap *const section = harbol_linkmap_new();
	return ( !section ) ? NULL : harbol_variant_new((union HarbolValue){ .LinkMapPtr=section }, HarbolTypeLinkMap);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_string(const char cstr[restrict])
{
	struct HarbolString *const restrict str = harbol_string_new_cstr(cstr);
	return ( !str ) ? NULL : harbol_variant_new((union HarbolValue){ .StrObjPtr=str }, HarbolTypeString);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_float(const double fltval)
{
	return harbol_variant_new((union HarbolValue){ .Double=fltval }, HarbolTypeFloat);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_int(const int64_t ival)
{
	return harbol_variant_new((union HarbolValue){ .Int64=ival }, HarbolTypeInt);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_bool(const bool boolval)
{
	return harbol_variant_new((union HarbolValue){ .Bool=boolval }, HarbolTypeBool);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_color(union HarbolColor *const color)
{
	const size_t size[] = { sizeof(union HarbolColor) };
	struct HarbolTuple *const color_tuple = harbol_tuple_new(1, size, false);
	harbol_tuple_set_field(color_tuple, 0, color);
	return ( !color_tuple ) ? NULL : harbol_variant_new((union HarbolValue){ .TuplePtr=color_tuple }, HarbolTypeColor);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_vec4d(union HarbolVec4D *const vec4d)
{
	const size_t size[] = { sizeof(union HarbolVec4D) };
	struct HarbolTuple *const vec_tuple = harbol_tuple_new(1, size, false);
	harbol_tuple_set_field(vec_tuple, 0, vec4d);
	return ( !vec_tuple ) ? NULL : harbol_variant_new((union HarbolValue){ .TuplePtr=vec_tuple }, HarbolTypeVec4D);
}

HARBOL_EXPORT struct HarbolVariant *harbol_cfg_create_null(void)
{
	return harbol_variant_new((union HarbolValue){ .Int64=0 }, HarbolTypeNull);
}
*/

static void _write_tabs(FILE *const file, const size_t tabs)
{
	for( size_t i=0; i<tabs; i++ )
		fputs("\t", file);
}

static bool _harbol_cfg_build_file(const struct HarbolLinkMap *const map, FILE *const file, const size_t tabs)
{
	if( !map || !file )
		return false;
	
	const union HarbolValue *const end = harbol_linkmap_get_iter_end_count(map);
	for( union HarbolValue *iter = harbol_linkmap_get_iter(map); iter && iter<end; iter++ ) {
		const struct HarbolKeyValPair *kv = iter->KvPairPtr;
		const int32_t type = kv->Data.VarPtr->TypeTag;
		_write_tabs(file, tabs);
		// print out key and notation.
		fprintf(file, "\"%s\": ", kv->KeyName.CStr);
		
		switch( type ) {
			case HarbolTypeNull:
				fputs("null\n", file); break;
			case HarbolTypeLinkMap:
				fputs("{\n", file);
				_harbol_cfg_build_file(kv->Data.VarPtr->Val.LinkMapPtr, file, tabs+1);
				_write_tabs(file, tabs);
				fputs("}\n", file);
				break;
			
			case HarbolTypeString:
				fprintf(file, "\"%s\"\n", kv->Data.VarPtr->Val.StrObjPtr->CStr); break;
			case HarbolTypeFloat:
				fprintf(file, "%f\n", kv->Data.VarPtr->Val.Double); break;
			case HarbolTypeInt:
				fprintf(file, "%" PRIi64 "\n", kv->Data.VarPtr->Val.Int64); break;
			case HarbolTypeBool:
				fprintf(file, "%s\n", kv->Data.VarPtr->Val.Bool ? "true" : "false"); break;
			
			case HarbolTypeColor: {
				struct { uint8_t r,g,b,a; } color = {0};
				harbol_tuple_to_struct(kv->Data.VarPtr->Val.TuplePtr, &color);
				fprintf(file, "c[ %u, %u, %u, %u ]\n", color.r, color.g, color.b, color.a);
				break;
			}
			case HarbolTypeVec4D: {
				struct { float x,y,z,w; } vec4 = {0};
				harbol_tuple_to_struct(kv->Data.VarPtr->Val.TuplePtr, &vec4);
				fprintf(file, "v[ %f, %f, %f, %f ]\n", vec4.x, vec4.y, vec4.z, vec4.w);
				break;
			}
		}
	}
	return true;
}

HARBOL_EXPORT bool harbol_cfg_build_file(const struct HarbolLinkMap *const restrict cfg, const char filename[restrict], const bool overwrite)
{
	if( !cfg || !filename )
		return false;
	
	FILE *restrict cfgfile = fopen(filename, overwrite ? "w+" : "a+");
	if( !cfgfile ) {
		fputs("harbol_cfg_build_file :: unable to create file.\n", stderr);
		return false;
	}
	const bool result = _harbol_cfg_build_file(cfg, cfgfile, 0);
	fclose(cfgfile), cfgfile=NULL;
	return result;
}
