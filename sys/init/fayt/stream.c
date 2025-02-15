#include <fayt/stream.h>
#include <fayt/string.h>

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

static void stream_write_number(struct stream_info *stream, uint64_t nunmber, int base);

int stream_print(struct stream_info *stream, const char *str, va_list arg) {
	if(stream == NULL || stream->write == NULL) return -1;
	
	for(size_t i = 0; i < strlen(str); i++) {
		if(str[i] != '%') {
			stream->write(stream, str[i]);
			continue;
		}

		switch(str[++i]) {
			case 'd': {
				uint64_t number = va_arg(arg, uint64_t);
				stream_write_number(stream, number, 10);
				break;
			}
			case 's': {
				const char *s = va_arg(arg, const char*); 

				if(!stream) {
					stream->write(stream, 'N'); stream->write(stream, 'U'); stream->write(stream, 'L'); stream->write(stream, 'L');
					break;
				}

				for(size_t i = 0; i < strlen(s); i++) {
					stream->write(stream, s[i]);
				}

				break;
			}
			case 'c': {
				char c = va_arg(arg, int);
				stream->write(stream, c);
				break;
			}
			case 'x': {
				uint64_t number = va_arg(arg, uint64_t);
				stream_write_number(stream, number, 16);
				break;
			}
			case 'b': {
				uint64_t number = va_arg(arg, uint64_t);
				stream_write_number(stream, number, 2);
				break;
			}
		}
	}

	return 0;
}

static void stream_write_number(struct stream_info *stream, uint64_t number, int base) {
	static char characters[] = "0123456789ABCDEF";
	int arr[50], cnt = 0;

	do {
		arr[cnt++] = number % base;
		number /= base;
	} while(number);

	for(int i = cnt - 1; i > -1; i--) {
		stream->write(stream, characters[arr[i]]);
	}
}
