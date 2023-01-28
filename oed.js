// oed.js
// Dale Schumacher
// 2023-01-27
// Public Domain

const false_octet =     0b1000_0000;    // 128 = false
const true_octet =      0b1000_0001;    // 129 = true
const num_sign_bit =    0b0000_0001;    //   1 = Number sign (0=positive, 1=negative)
const integer_octet =   0b1000_0010;    // 130 = Number (positive Integer)
const decimal_octet =   0b1000_0100;    // 132 = Number (positive Decimal)
const rational_octet =  0b1000_0110;    // 134 = Number (positive Rational)
const array_octet =     0b1000_1000;    // 136 = Array
const object_octet =    0b1000_1001;    // 137 = Object
const blob_octet =      0b1000_1010;    // 138 = String (blob)
const extension_octet = 0b1000_1011;    // 139 = String (extension)
const string_octet =    0b1000_1100;    // 140 = String (utf8)
const null_octet =      0b1000_1111;    // 143 = null

const radix = 256;
const utf8encoder = new TextEncoder();
const utf8decoder = new TextDecoder();

function encode_integer(integer) {
    console.log("encode_integer", integer);
    if (integer < 0) {
        // negative integer
        if (integer >= -112) return new Uint8Array([radix + integer]);  // small negative integer
        integer = -integer - 1;
        const digits = [];
        while (integer > 0) {
            const digit = (integer % radix) ^ 0xFF;
            digits.push(digit);
            integer = Math.floor(integer / radix);
        }
        digits.unshift((integer_octet | num_sign_bit), digits.length * 8);
        return new Uint8Array(digits);
    } else {
        // non-negative integer
        if (integer <= 127) return new Uint8Array([integer]);  // small positive integer
        const digits = [];
        while (integer > 0) {
            const digit = (integer % radix);
            digits.push(digit);
            integer = Math.floor(integer / radix);
        }
        digits.unshift(integer_octet, digits.length * 8);
        return new Uint8Array(digits);
    }
}
function encode_string(string) {
    console.log("encode_string", string);
    let length = 0;
    for (const codepoint of string) {
        length += 1;
    }
    if (length === 0) {
        return new Uint8Array([string_octet, 0]);  // empty string
    }
    const length_octets = encode_integer(length);
    const data_octets = utf8encoder.encode(string);
    const size_octets = encode_integer(data_octets.length);
    const octets = new Uint8Array(1 + length_octets.length + size_octets.length + data_octets.length);
    octets.set([string_octet], 0);
    octets.set(length_octets, 1);
    octets.set(size_octets, 1 + length_octets.length);
    octets.set(data_octets, 1 + length_octets.length + size_octets.length);
    return octets;
}
function encode_array(array) {
    console.log("encode_array", array);
    if (array.length === 0) {
        return new Uint8Array([array_octet, 0]);  // empty array
    }
    let size = 0;
    const elements = [];
    array.forEach((element) => {
        let octets = encode(element);
        if (octets === undefined) {
            octets = new Uint8Array([null_octet]);  // replace error with null element
        }
        size += octets.length;
        elements.push(octets);
    });
    console.log("encode_array: elements =", elements);
    const length_octets = encode_integer(elements.length);
    const size_octets = encode_integer(size);
    const octets = new Uint8Array(1 + length_octets.length + size_octets.length + size);
    let offset = 0;
    octets.set([array_octet], offset);
    offset += 1;
    octets.set(length_octets, offset);
    offset += length_octets.length;
    octets.set(size_octets, offset);
    offset += size_octets.length;
    elements.forEach((element) => {
        octets.set(element, offset);
        offset += element.length;
    });
    return octets;
}
function encode_object(object) {
    console.log("encode_object", object);
    const keys = Object.keys(object);
    if (keys.length === 0) {
        return new Uint8Array([object_octet, 0]);  // empty object
    }
    let size = 0;
    const members = [];
    keys.forEach((key) => {
        const value_octets = encode(object[key]);
        if (value_octets !== undefined) {  // skip non-encodeable values
            const key_octets = encode_string(key);
            size += key_octets.length;
            members.push(key_octets);
            size += value_octets.length;
            members.push(value_octets);
        }
    });
    console.log("encode_object: members =", members);
    const length_octets = encode_integer(members.length / 2);
    const size_octets = encode_integer(size);
    const octets = new Uint8Array(1 + length_octets.length + size_octets.length + size);
    let offset = 0;
    octets.set([object_octet], offset);
    offset += 1;
    octets.set(length_octets, offset);
    offset += length_octets.length;
    octets.set(size_octets, offset);
    offset += size_octets.length;
    members.forEach((member) => {
        octets.set(member, offset);
        offset += member.length;
    });
    return octets;
}
function encode(value) {
    if (value === false) return new Uint8Array([false_octet]);
    if (value === true) return new Uint8Array([true_octet]);
    if (value === null) return new Uint8Array([null_octet]);
    if (Number.isSafeInteger(value)) return encode_integer(value);
    //if (Number.isFinite(value)) return encode_float(value);
    if (typeof value === "string") return encode_string(value);
    //if (value?.constructor === Uint8Array) return encode_blob(value);
    if (Array.isArray(value)) return encode_array(value);
    if (typeof value === "object") return encode_object(value);
    //return undefined;  // default: encode failed
}

function compose_number({ integer, exponent = 0, base = 10 }) {
    return integer * (base ** exponent);
}
function decode_integer({ octets, offset }) {
    let number = decode_number({ octets, offset });
    if (number.error) return number;  // report error
    if (Number.isSafeInteger(number.value)) {
        return number;
    }
    return { error: "integer value required", octets, offset, value: number.value };
}
function decode_number({ octets, offset }) {
/*
`2#0xxx_xxxx` | -                                                          | positive small integer (0..127)
`2#1000_0010` | _size_::Number _int_::Octet\*                              | Number (positive integer)
`2#1000_0011` | _size_::Number _int_::Octet\*                              | Number (negative integer)
`2#1000_0100` | _exp_::Number _size_::Number _int_::Octet\*                | Number (positive decimal)
`2#1000_0101` | _exp_::Number _size_::Number _int_::Octet\*                | Number (negative decimal)
`2#1000_0110` | _base_::Number _exp_::Number _size_::Number _int_::Octet\* | Number (positive rational)
`2#1000_0111` | _base_::Number _exp_::Number _size_::Number _int_::Octet\* | Number (negative rational)
`2#1001_xxxx` | -                                                          | negative small integer (-112..-97)
`2#101x_xxxx` | -                                                          | negative small integer (-96..-65)
`2#11xx_xxxx` | -                                                          | negative small integer (-64..-1)
*/
    let prefix = octets[offset];
    if (typeof prefix !== "number") return { error: "offset out-of-bounds", octets, offset };
    offset += 1;
    if (prefix <= 0b0111_1111) return { value: prefix, octets, offset };
    if (prefix >= 0b1001_0000) return { value: (prefix - radix), octets, offset };
    let sign = (prefix & num_sign_bit) ? -1 : 1;
    prefix &= ~num_sign_bit;  // mask off sign bit
    let base = 10;
    let exponent = 0;
    if (prefix === rational_octet) {
        base = decode_integer({ octets, offset });
        if (base.error) return base;  // report error
        offset = base.offset;
        base = base.value;
        exponent = decode_integer({ octets, offset });
        if (exponent.error) return exponent;  // report error
        offset = exponent.offset;
        exponent = exponent.value;
    } else if (prefix === decimal_octet) {
        exponent = decode_integer({ octets, offset });
        if (exponent.error) return exponent;  // report error
        offset = exponent.offset;
        exponent = exponent.value;
    } else if (prefix !== integer_octet) {
        return { error: "unrecognized OED number", octets, offset: offset - 1 };
    }
    const size = decode_integer({ octets, offset });
    if (size.error) return size;  // report error
    let bits = size.value;
    offset = size.offset;
    let value = 0;
    let scale = 1;
    if (sign < 0) {
        // negative integer
        while (bits > 0) {
            value += scale * (octets[offset] ^ 0xFF);
            offset += 1;
            scale *= radix;
            bits -= 8;
        }
        value = -(value + 1);
    } else {
        // non-negative integer
        while (bits > 0) {
            value += scale * octets[offset];
            offset += 1;
            scale *= radix;
            bits -= 8;
        }
    }
    if (offset != (size.offset + (size.value / 8))) {
        return { error: "offset does not match OED number size", octets, offset };
    }
    value = compose_number({ integer: value, base, exponent });
    return { value, octets, offset };
}
function decode_string({ octets, offset }) {
/*
`2#1000_1010` | _size_::Number _data_::Octet\*                             | String (Raw BLOB)
`2#1000_1011` | _meta_::Value _size_::Number _data_::Octet\*               | String (Extension BLOB)
`2#1000_1100` | _length_::Number _size_::Number _data_::Octet\*            | String (UTF-8)
`2#1000_1101` | _length_::Number _size_::Number _data_::Octet\*            | String (UTF-8 +memo)
`2#1000_1110` | _index_::Octet                                             | String (memo reference)
*/
    const prefix = octets[offset];
    if (typeof prefix !== "number") return { error: "offset out-of-bounds", octets, offset };
    if (prefix === string_octet) {
        // utf8-encoded String
        const length = decode_integer({ octets, offset: offset + 1 });
        if (length.error) return length;  // report error
        if (length.value === 0) {
            return { value: "", octets, offset: length.offset };  // empty string
        }
        const size = decode_integer({ octets, offset: length.offset });
        if (size.error) return size;  // report error
        const data = octets.subarray(size.offset, (size.offset + size.value));
        const value = utf8decoder.decode(data);
        if (typeof value === "string") {
            return { value, octets, offset: (size.offset + size.value) };
        }
    }
    return { error: "unrecognized OED string", octets, offset };
}
function decode_array({ octets, offset }) {
/*
`2#1000_1000` | _length_::Number _size_::Number _elements_::Value\*        | Array
*/
    const prefix = octets[offset];
    if (typeof prefix !== "number") return { error: "offset out-of-bounds", octets, offset };
    if (prefix === array_octet) {
        // Array
        const length = decode_integer({ octets, offset: offset + 1 });
        if (length.error) return length;  // report error
        if (length.value === 0) {
            return { value: [], octets, offset: length.offset };  // empty array
        }
        const size = decode_integer({ octets, offset: length.offset });
        if (size.error) return size;  // report error
        const value = new Array(length.value);
        let index = 0;
        offset = size.offset;
        while (index < length.value) {
            const element = decode({ octets, offset });
            if (element.error) return element;  // report error
            value[index] = element.value;
            offset = element.offset;
            index += 1;
        }
        if (offset != (size.offset + size.value)) {
            return { error: "offset does not match OED array size", octets, offset };
        }
        return { value, octets, offset };
    }
    return { error: "unrecognized OED array", octets, offset };
}
function decode_object({ octets, offset }) {
/*
`2#1000_1001` | _length_::Number _size_::Number _members_::Octet\*         | Object
*/
    const prefix = octets[offset];
    if (typeof prefix !== "number") return { error: "offset out-of-bounds", octets, offset };
    if (prefix === object_octet) {
        // Object
        const length = decode_integer({ octets, offset: offset + 1 });
        if (length.error) return length;  // report error
        if (length.value === 0) {
            return { value: {}, octets, offset: length.offset };  // empty object
        }
        const size = decode_integer({ octets, offset: length.offset });
        if (size.error) return size;  // report error
        const value = {};
        let index = 0;
        offset = size.offset;
        while (index < length.value) {
            const key = decode_string({ octets, offset });
            if (key.error) return key;  // report error
            offset = key.offset;
            const member = decode({ octets, offset });
            if (member.error) return member;  // report error
            value[key.value] = member.value;
            offset = member.offset;
            index += 1;
        }
        if (offset != (size.offset + size.value)) {
            return { error: "offset does not match OED object size", octets, offset };
        }
        return { value, octets, offset };
    }
    return { error: "unrecognized OED object", octets, offset };
}
function decode({ octets, offset }) {
    if (octets === undefined) return { error: "missing property 'octets'" };
    const prefix = octets[offset];
    if (typeof prefix !== "number") return { error: "offset out-of-bounds", octets, offset };
    if (prefix === false_octet) return { value: false, octets, offset: offset + 1 };
    if (prefix === true_octet) return { value: true, octets, offset: offset + 1 };
    if (prefix === null_octet) return { value: null, octets, offset: offset + 1 };
    if (prefix <= 0b0111_1111) return { value: prefix, octets, offset: offset + 1 };
    if (prefix >= 0b1001_0000) return { value: (prefix - radix), octets, offset: offset + 1 };
    if ((prefix & ~num_sign_bit) === integer_octet) return decode_integer({ octets, offset });
    if (prefix === string_octet) return decode_string({ octets, offset });
    if (prefix === array_octet) return decode_array({ octets, offset });
    if (prefix === object_octet) return decode_object({ octets, offset });
    return { error: "unrecognized OED value", octets, offset };
}

export default Object.freeze({encode, decode});
