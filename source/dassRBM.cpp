#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lodepng.h>

// #define PAUSE
// #define REMOVE_EMPTY_ITEMS
// #define LZ16_ALPHA_HANDLING
#define LZ16_COMPATIBILITY
// #define LZ32_COMPATIBILITY
#define LZ32_MIN_LENGTH 2
#define TEMP_RGBA "temp.rgba"
#define BIG_FILE_SIZE ( 1 << 17 )
#define BUFFER_SIZE ( 1 << 10 )

unsigned char rbm_sof[ 8 ] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
unsigned char ptr_sof[ 4 ] = { 0x00, 0x01, 0x00, 0x00 };
unsigned char deadcode[ 4 ] = { 0xDE, 0xAD, 0xC0, 0xDE };


int read_int32( FILE* fp ) {
	unsigned char data[ 4 ];
	int value = 0;
	
	fread( data, 1, 4, fp );	
	value =
		( ( data[0] & 0xFF ) <<  0 ) |
		( ( data[1] & 0xFF ) <<  8 ) |
		( ( data[2] & 0xFF ) << 16 ) |
		( ( data[3] & 0xFF ) << 24 );
	
	return value;
}

void write_int32( FILE* fp, int value ) {
	unsigned char data[ 4 ];
	
	data[0] = ( value >>  0 ) & 0xFF;
	data[1] = ( value >>  8 ) & 0xFF;
	data[2] = ( value >> 16 ) & 0xFF;
	data[3] = ( value >> 24 ) & 0xFF;
	fwrite( data, 1, 4, fp );
	
	return;
}

bool png_to_rgba( const char* png, const char* rgba, int *w, int *h ) {
	unsigned char* rgba_data;
	int err_code;
	FILE* fp;
	
	
	err_code = lodepng_decode32_file( &rgba_data, (unsigned*) w, (unsigned*) h, png );
	if ( err_code ) return false;
	
	fp = fopen( rgba, "wb" );
	if ( fp == NULL ) {
		free( rgba_data );
		return false;
	}
	fwrite( rgba_data, 1, (*w)*(*h)*4, fp );
	fclose( fp );
	
	free( rgba_data );
	
	
	return true;
}

int copy_data( FILE* dest, FILE* src, int length ) {
	unsigned char buffer[ BUFFER_SIZE ];
	int len, c;
	
	if ( length >= 0 ) {
		for ( len = 0, c = 0; len < length; len += c ) {
			if ( length - len >= BUFFER_SIZE ) 
				c = fread( buffer, 1, BUFFER_SIZE, src );
			else c = fread( buffer, 1, length % BUFFER_SIZE, src );
			fwrite( buffer, 1, c, dest );
		}
	} else {
		len = 0;
		do {
			c = fread( buffer, 1, BUFFER_SIZE, src );
			fwrite( buffer, 1, c, dest );
			len += c;
		} while ( c == BUFFER_SIZE );
	}
	
	return len;
}

int compress_lz16_555( FILE* dest, FILE* src, int* mask ) {
	// LZ16 compress for 16 Bit 5-5-5 RGB image data
	unsigned char* rgb16_raw;
	unsigned char* code_seg;
	unsigned char* data_seg;
	unsigned char* code_ptr;
	unsigned char* data_ptr;
	unsigned char offset[4];
	unsigned char rgb24[3];
	unsigned char rgb32[4];
	#ifdef LZ16_ALPHA_HANDLING
	unsigned char a16[2];
	unsigned char a24[3];
	#endif
	int lz_dist, lz_count;
	int mode = 1; // 0 -> code, 1 -> data
	int npix = 0;
	int p, i, j;
	int len;
	
	
	#ifdef LZ16_ALPHA_HANDLING
	// transparency handling
	if ( mask != NULL ) {
		a24[0] = ( *mask >> 16 ) & 0xFF;
		a24[1] = ( *mask >>  8 ) & 0xFF;
		a24[2] = ( *mask >>  0 ) & 0xFF;
		a16[1] = ( ( a24[0] << 0 ) & 0xF8 ) | ( ( a24[1] >> 5 ) & 0x07 );
		a16[0] = ( ( a24[1] << 3 ) & 0xC0 ) | ( ( a24[2] >> 3 ) & 0x1F );
		*mask = ( a16[1] << 8 ) | ( a16[0] << 0) ;
	}
	#endif
	
	// convert and store in memory: 24 bit RAW (RGB, interleaved) to 16 bit RAW
	rgb16_raw = ( unsigned char* ) calloc( 1 << 16, 1 );
	while( fread( rgb32, 1, 4, src ) == 4 ) {
		rgb24[0] = ( rgb32[0] * rgb32[3] ) / 0xFF;
		rgb24[1] = ( rgb32[1] * rgb32[3] ) / 0xFF;
		rgb24[2] = ( rgb32[2] * rgb32[3] ) / 0xFF;
		#ifdef LZ16_ALPHA_HANDLING
		if ( mask != NULL ) if ( rgb32[3] < 0x80 ) {
			rgb24[0] = a24[0];
			rgb24[1] = a24[1];
			rgb24[2] = a24[2];
		}
		#endif
		rgb16_raw[(npix*2)+1] = ( ( rgb24[0] << 0 ) & 0xF8 ) | ( ( rgb24[1] >> 5 ) & 0x07 );
		rgb16_raw[(npix*2)+0] = ( ( rgb24[1] << 3 ) & 0xC0 ) | ( ( rgb24[2] >> 3 ) & 0x1F );
		if ( ((++npix)*2) % (1<<16) == 0 )
			rgb16_raw = ( unsigned char* ) realloc( rgb16_raw, (npix*2) + (1<<16) );
		#ifdef LZ16_ALPHA_HANDLING
		// prevent alpha collisions
		if ( mask != NULL ) if ( memcmp( rgb24, a24, 3 ) == 0 )
			if ( memcmp( rgb16_raw + (npix*2), a16, 2 ) == 0 )
				rgb16_raw[(npix*2)+0] ^= 0x40;
		#endif
	}
	
	// alloc memory for code and data segments
	code_seg = ( unsigned char* ) calloc( (npix*2), 1 );
	data_seg = ( unsigned char* ) calloc( (npix*2), 1 );
	code_ptr = code_seg; data_ptr = data_seg;
	
	// main conversion loop
	for ( p = 0; p < npix; p++ ) {
		// find best match
		lz_dist = 0; lz_count = 0;
		for ( i = 1; i < (1<<11); i++ ) { // lz distance
			if ( i > p ) break;
			for ( j = 0; memcmp( rgb16_raw + ((p+j)*2), rgb16_raw + ((p+j-i)*2 ), 2 ) == 0; j++ ) 
				if ( ( j == (1<<4)-1 ) || ( (p+j) >= (npix-1) ) ) break; // lz count
			if ( lz_count < j ) {
				lz_dist = i;
				lz_count = j;
				if ( lz_count == (1<<4)-1 ) break;
			}
		}
		// write code / data segments
		if ( lz_count <= 1 ) { // no good match, copy image data to data segment
			*(data_ptr++) = rgb16_raw[(p*2)+0];
			*(data_ptr++) = rgb16_raw[(p*2)+1];
			#ifdef LZ16_COMPATIBILITY
			*(code_ptr++) = 0x08;
			*(code_ptr++) = 0x00;
			*(data_ptr-2) |= 0x20;
			// no mode switching
			// maybe cleaner implementation later
			#else
			if ( mode == 0 ) { // previous mode was LZ codes
				*(code_ptr-2) |= 0x08;
				mode = 1;
			}
			#endif
		} else { // match found, generate code in code segment
			*(code_ptr++) = ( ( lz_count << 4 ) & 0xF0 ) | ( ( lz_dist >> 8 ) & 0x07 );
			*(code_ptr++) = ( lz_dist & 0xFF );
			if ( mode == 1 ) { // previous mode was data
				*(data_ptr-2) |= 0x20;
				mode = 0;
			}
			// advance pixel position
			p += lz_count - 1;
		}
	}
	// set last flag (data pointer)
	*(data_ptr-2) |= 0x20;
	
	// all pixels processed, calculate offset (offset total: 15 / 30 bit)
	offset[0] = ( ( code_ptr - code_seg ) & 0x0000FF00 ) >>  8;
	offset[1] = ( ( code_ptr - code_seg ) & 0x000000FE ) >>  0;
	if ( ( code_ptr - code_seg ) >= 1<<16 ) { // 4 byte offset
		offset[2] = ( ( code_ptr - code_seg ) & 0x00EF0000 ) >> 15;
		offset[3] = ( ( code_ptr - code_seg ) & 0x0EF10000 ) >> 23;
		offset[1] |= 1;
	}
	
	// write offset ( 2/4 Byte )
	fwrite( offset, 1, ( offset[1] & 1 ) ? 4 : 2, dest );
	
	// write code and data segments
	fwrite( code_seg, 1, code_ptr - code_seg, dest );
	fwrite( data_seg, 1, data_ptr - data_seg, dest );
	
	// calculate length
	len = ( offset[1] & 1 ) ? 4 : 2;
	len += code_ptr - code_seg;
	len += data_ptr - data_seg;
	
	// free memory
	free( rgb16_raw );
	free( code_seg );
	free( data_seg );
	
	
	return len;
}

int compress_lz16_rgba( FILE* dest, FILE* src ) {
	// LZ16 compress for 32 Bit RGBA image data
	unsigned char* rgb32_raw;
	unsigned char* code_seg;
	unsigned char* data_seg;
	unsigned char* ext_seg;
	unsigned char* code_ptr;
	unsigned char* data_ptr;
	unsigned char* ext_ptr;
	unsigned char offset_d[4];
	unsigned char offset_e[4];
	unsigned char rgb32[4];
	int lz_dist, lz_count;
	int mode = 1; // 0 -> code, 1 -> data
	int nbyte = 0;
	int npix = 0;
	int p, i, j;
	int len;
	
	
	// convert and store in memory: RGBA to BGRA
	rgb32_raw = ( unsigned char* ) calloc( 1 << 16, 1 );
	while( fread( rgb32, 1, 4, src ) == 4 ) {
		rgb32_raw[(npix*4)+0] = rgb32[2];
		rgb32_raw[(npix*4)+1] = rgb32[1];
		rgb32_raw[(npix*4)+2] = rgb32[0];
		rgb32_raw[(npix*4)+3] = rgb32[3];
		if ( ((++npix)*4) % (1<<16) == 0 )
			rgb32_raw = ( unsigned char* ) realloc( rgb32_raw, (npix*4) + (1<<16) );
	}
	
	// alloc memory for code, data and ext segments
	nbyte = npix * 4;
	code_seg = ( unsigned char* ) calloc( nbyte, 1 );
	data_seg = ( unsigned char* ) calloc( nbyte + 2, 1 );
	ext_seg = ( unsigned char* ) calloc( 2 + ((nbyte+7)/8), 1 );
	code_ptr = code_seg;
	data_ptr = data_seg;
	ext_ptr = ext_seg - 1;
	
	// main conversion loop
	for ( p = 0; p < nbyte; p += 2 ) {
		// find best match
		lz_dist = 0; lz_count = 0;
		for ( i = 2; i < (1<<8); i++ ) { // lz distance
			if ( (i*2) > p ) break;
			for ( j = 0; memcmp( rgb32_raw+p+(j*2), rgb32_raw+p+((j-i)*2), 2 ) == 0; j++ ) 
				if ( ( j == (1<<6)-1 ) || ( (p+(j*2)) >= (nbyte-4) ) ) break; // lz count
			if ( lz_count < j ) {
				lz_dist = i;
				lz_count = j;
				if ( lz_count == (1<<6)-1 ) break;
			}
		}
		// write code / data segments
		if ( lz_count <= (LZ32_MIN_LENGTH-1) ) { // no good match, copy image data to data segment
			if ( ( ( data_ptr - data_seg ) % 8 ) == 0 ) ext_ptr++;
			*(data_ptr++) = rgb32_raw[p+0];
			*(data_ptr++) = rgb32_raw[p+1];
			*(ext_ptr) >>= 2;
			if ( mode == 0 ) { // previous mode was LZ codes
				*(code_ptr-2) |= 0x80;
				mode = 1;
			}
		} else { // match found, generate code in code segment
			*(code_ptr++) = ( (lz_count<<1) & 0x7E );
			*(code_ptr++) = ( lz_dist & 0xFF );
			if ( mode == 1 ) { // previous mode was data
				*(ext_ptr) |= 0x40;
				mode = 0;
			}
			// advance byte position
			p += (lz_count-1) * 2;
		}
	}
	// fill ext segment to next full byte
	*(ext_ptr) |= 0x40;
	for ( p = data_ptr - data_seg; (p%8) != 0; p += 2 )
		*(ext_ptr) >>= 2;
	ext_ptr++;
	// finish code pointer
	if ( code_ptr != code_seg )
		*(code_ptr-2) |= 0x80;
	#ifdef LZ32_COMPATIBILITY
	// add crap data
	*(data_ptr++) = *(data_ptr-2);
	*(data_ptr++) = *(data_ptr-2);
	*(ext_ptr++)  = 0x00;
	*(ext_ptr++)  = 0x00;
	#endif
	
	// all pixels processed, calculate offsets (offset total: 30 bit)
	offset_d[0] = ( ( code_ptr - code_seg ) & 0x0000FF00 ) >>  8;
	offset_d[1] = ( ( code_ptr - code_seg ) & 0x000000FE ) >>  0;
	offset_d[2] = ( ( code_ptr - code_seg ) & 0x00EF0000 ) >> 15;
	offset_d[3] = ( ( code_ptr - code_seg ) & 0x0EF10000 ) >> 23;
	offset_e[0] = ( ( data_ptr - data_seg ) & 0x0000FF00 ) >>  8;
	offset_e[1] = ( ( data_ptr - data_seg ) & 0x000000FE ) >>  0;
	offset_e[2] = ( ( data_ptr - data_seg ) & 0x00EF0000 ) >> 15;
	offset_e[3] = ( ( data_ptr - data_seg ) & 0x0EF10000 ) >> 23;
	offset_d[1] |= 0x1;
	
	// write offsets for data and ext
	fwrite( offset_d, 1, 4, dest );
	fwrite( offset_e, 1, 4, dest );
	
	// write code, data and ext segments
	fwrite( code_seg, 1, code_ptr - code_seg, dest );
	fwrite( data_seg, 1, data_ptr - data_seg, dest );
	fwrite( ext_seg, 1, ext_ptr - ext_seg, dest );
	
	// calculate length
	len = 8;
	len += code_ptr - code_seg;
	len += data_ptr - data_seg;
	len += ext_ptr - ext_seg;
	
	// free memory
	free( rgb32_raw );
	free( code_seg );
	free( data_seg );
	free( ext_seg );
	
	
	return len;
}


int main( int argc, char** argv )
{
	// strings and files
	char line[ 256 + 1 ];
	char fn[ 260 + 1 ]; // file name of output file
	FILE* fp_inp;
	FILE* fp_lst;
	FILE* fp_out;
	// settings
	bool dass = true; // disassemble or assemble
	bool summary = false; // summarize?
	// file positions
	int pos0 = 0; // start adress of file
	int len0 = 0; // length of file
	int posr = 0; // return adress in header
	// RBM item stuff
	int i_id = 0;
	int i_type = 0;
	int i_hor = 0;
	int i_ver = 0;
	int i_bpp = 0;
	int i_alpha = 0;
	int i_mask = 0;
	// summary stuff
	int n0x0 = 0;
	int n1x1 = 0;
	int nhxh = 0;
	int bigf = 0;
	int msize = 0;
	// other
	int nf = 0;
	int c;
	
	
	// headline
	fprintf( stdout, "dassRBM v0.9b by _king_\n" );
	if ( argc < 4 ) {
		fprintf( stdout, "Usage: %s [-d|-a] [input file] [list file]\n", argv[ 0 ] );
		exit( 0 );
	}
	
	// set mode
	if ( strcmp( argv[1], "-a" ) == 0 ) dass = false;
	if ( strcmp( argv[1], "-s" ) == 0 ) summary = true;
	
	// write status message to screen
	fprintf( stdout, "\n%s file \"%s\" using list \"%s\"...\n",
		( dass ) ? "Disassembling" : "Assembling",
		argv[2],
		argv[3] );	
	
	
	if ( dass ) { // disassembling mode
		// open files
		fp_inp = fopen( argv[2], "rb" );
		fp_lst = fopen( argv[3], ( summary ) ? "ab" : "wb" );
		if ( ( fp_inp == NULL ) || ( fp_lst == NULL ) ) {
			fprintf( stderr, "file access error!\n" );
			exit( 1 );
		}
		
		// check RBM header
		while ( true ) {
			if ( read_int32( fp_inp ) == 0x00 )
			if ( read_int32( fp_inp ) == 0x100 )
				break;
			fprintf( stderr, "not a RBM file!\n" );
			exit( 1 );
		}		
		
		// get file count
		nf = read_int32( fp_inp );
		
		// init list file
		if ( !summary ) fprintf( fp_lst, "#id;width;height;bpp;type;alpha;mask;name\n" );
		else {
			fseek( fp_lst, 0, SEEK_END );
			if ( ftell( fp_lst ) == 0 )	fprintf( fp_lst,
				"source;id;size;offset;width;height;bpp;type;alpha;mask\n" );
		}
		
		// main processing loop
		posr = ftell( fp_inp );
		for ( int i = 0; i < nf; i++ ) {
			// return to header
			fseek( fp_inp, posr, SEEK_SET );
			// check PTR header
			while ( read_int32( fp_inp ) != 0x100 ) {
				fprintf( stderr, "error in RBM header!\n" );
				exit( 1 );
			}
			// item id
			i_id = read_int32( fp_inp );
			// get item position and end
			pos0 = read_int32( fp_inp );
			len0 = read_int32( fp_inp );
			// jump to item
			posr = ftell( fp_inp );
			fseek( fp_inp, pos0, SEEK_SET );
			// first run: check item header data
			while ( true ) {
				c = 0;
				if ( read_int32( fp_inp ) != i_id ) break;
				i_type = read_int32( fp_inp );
				if ( i_type == 1 ) {
					if ( read_int32( fp_inp ) != len0 - 0x28 ) break;
					i_type = read_int32( fp_inp );
					i_hor = read_int32( fp_inp );
					i_ver = read_int32( fp_inp );
					i_bpp = read_int32( fp_inp );
					i_alpha = read_int32( fp_inp );
					i_mask = read_int32( fp_inp );
				} else if ( i_type == -1 ) {
					i_type = 0;	i_hor = 0; i_ver = 0;
					i_bpp = 0; i_alpha = 0; i_mask = 0;
					if ( len0 != 0x28 ) break;
					for ( int j = 0; j < 7; j++ )
						if ( read_int32( fp_inp ) != -1 ) break;
				} else break;
				if ( read_int32( fp_inp ) != 0x1C ) break;
				c = 1; break;
			}
			if ( c != 1 ) {
				fprintf( stderr, "error in item header!\n" );
				exit( 1 );
			}
			if ( i_type != 0 ) {
				c = ( strrchr( argv[2], '.' ) != NULL ) ?
					( strrchr( argv[2], '.' ) - argv[2] ) : strlen( argv[2]  );
				memcpy( fn, argv[2], c );
				sprintf( fn + c, "_%.3i_%ix%ix%i.%s", i_id, i_hor, i_ver, i_bpp,
					( i_type == 2 ) ? "img" : "qmg" );
			} else strcpy( fn, "empty.bin" ); 
			fprintf( stdout, "%.3i: %s -> ", i, fn );
			// second run: copy data (excluding header)
			if ( !summary ) {
				if ( i_type != 0 ) {
					fp_out = fopen( fn, "wb" );
					if ( len0 - 0x28 != copy_data( fp_out, fp_inp, len0 - 0x28 ) ) {
						fprintf( stdout, "ERROR\n" );
						fprintf( stderr, "unexpected end of file!\n" );
						exit( 1 );
					}
					fclose( fp_out );
				}
				fprintf( fp_lst, "%i;%i;%i;%i;%i;%i;0x%08X;%s\n",
					i_id, i_hor, i_ver, i_bpp, i_type, i_alpha, i_mask, fn );
			} else {
				fprintf( fp_lst, "%s;%i;%i;0x%08X;%i;%i;%i;%i;%i;0x%08X\n",
					argv[2], i_id, len0 - 0x28, pos0, i_hor, i_ver, i_bpp, i_type, i_alpha, i_mask );
			}
			fprintf( stdout, "%i byte (%i%%)\n", len0,
				( i_hor*i_ver == 0 ) ? 0 : (100*len0) / (4*i_hor*i_ver) );
			// additional stuff: summary
			if ( ( i_hor == 0 ) && ( i_ver == 0 ) ) n0x0++;
			else if ( ( i_hor == 1 ) && ( i_ver == 1 ) ) n1x1++;
			else nhxh++;
			if ( ( len0 - 0x28 ) > BIG_FILE_SIZE ) bigf++;
			if ( ( len0 - 0x28 ) > msize ) msize = len0 - 0x28;
		}
		if ( ( summary ) && ( argc > 4 ) ) {
			fp_out = fopen( argv[4], "ab" );
			fseek( fp_inp, 0, SEEK_END );
			fseek( fp_out, 0, SEEK_END );
			if ( ftell( fp_out ) == 0 ) fprintf( fp_out,
				"name;#items;total_size;#big_items;max_size;#items_0x0;#items_1x1;#items_higher\n" );
			fprintf( fp_out, "%s;%i;%i;%i;%i;%i;%i;%i\n",
				argv[2], nf, (int) ftell( fp_inp ), bigf, msize, n0x0, n1x1, nhxh );
			fclose( fp_out );
		}
	} else { // assembling mode
		// open files
		fp_inp = fopen( argv[2], "wb" );
		fp_lst = fopen( argv[3], "rb" );
		if ( ( fp_inp == NULL ) || ( fp_lst == NULL ) ) {
			fprintf( stderr, "file access error!\n" );
			exit( 1 );
		}
		
		// first pass: build file header placeholder
		write_int32( fp_inp, 0x00 );
		write_int32( fp_inp, 0x100 );
		write_int32( fp_inp, 0x00 ); // file count - return later
		for ( nf = 0; fgets( line, 256, fp_lst ) != NULL; ) {
			if ( line[0] == '#' ) continue;
			if ( sscanf( line, "%i;%i;%i;%i;%i;%i;%i;%s",
				&i_id, &i_hor, &i_ver, &i_bpp, &i_type, &i_alpha, &i_mask, fn ) != 8 ) break;
			write_int32( fp_inp, 0x100 );
			write_int32( fp_inp, i_id );
			// write placeholder data
			write_int32( fp_inp, 0x00 );
			write_int32( fp_inp, 0x00 );
			nf++;
		}
		fseek( fp_lst, 0x0, SEEK_SET );
		fseek( fp_inp, 0x8, SEEK_SET );
		write_int32( fp_inp, nf );
		
		// second pass: copy actual files, fill header
		for ( int i = 0; i < nf; ) {
			fgets( line, 256, fp_lst );
			if ( line[0] == '#' ) continue;
			sscanf( line, "%i;%i;%i;%i;%i;%i;%i;%s",
				&i_id, &i_hor, &i_ver, &i_bpp, &i_type, &i_alpha, &i_mask, fn );
			fprintf( stdout, "%.3i: %s -> ", i_id, fn );
			if ( i_type == 0 ) {
				#ifndef REMOVE_EMPTY_ITEMS
				// move to correct position
				fseek( fp_inp, 0x0, SEEK_END );
				pos0 = ftell( fp_inp );
				len0 = 0x28;
				// fill with 0xFF
				write_int32( fp_inp, i_id );
				for ( int j = 0; j < 8; j++ )
					write_int32( fp_inp, -1 );
				write_int32( fp_inp, 0x1C );
				// update RBM header
				fseek( fp_inp, 20 + (16*i), SEEK_SET );
				write_int32( fp_inp, pos0 );
				write_int32( fp_inp, len0 );
				#endif
			} else {
				if ( i_type == 1 ) { // convert PNG to RGBA first
					if ( !png_to_rgba( fn, TEMP_RGBA, &i_hor, &i_ver ) ) {
						fprintf( stderr, "ERROR\n" );
						fprintf( stderr, "not a valid PNG file!\n" );
						exit( 1 );
					}
					fp_out = fopen( TEMP_RGBA, "rb" );
				} else fp_out = fopen( fn, "rb" );
				if ( fp_out != NULL ) {
					// move to correct position
					fseek( fp_inp, 0x0, SEEK_END );
					pos0 = ftell( fp_inp );
					// write item header and item
					write_int32( fp_inp, i_id );
					write_int32( fp_inp, 0x01 );
					write_int32( fp_inp, 0x00 ); // placeholder
					write_int32( fp_inp, ( i_type == 1 ) ? 2 : i_type );
					write_int32( fp_inp, i_hor );
					write_int32( fp_inp, i_ver );
					write_int32( fp_inp, i_bpp );
					write_int32( fp_inp, i_alpha );
					write_int32( fp_inp, i_mask );
					write_int32( fp_inp, 0x1C );
					if ( i_type == 1 ) { // RGBA image conversions 
						if ( i_bpp == 16 ) { // 16 bit per pixel
							len0 = compress_lz16_555( fp_inp, fp_out, ( i_alpha ) ? &i_mask : NULL );
							fseek( fp_inp, pos0 + 0x20, SEEK_SET );
							write_int32( fp_inp, i_mask );
						} else if ( i_bpp == 32 ) {
							len0 = compress_lz16_rgba( fp_inp, fp_out );
						} else {
							fprintf( stderr, "ERROR\n" );
							fprintf( stderr, "converting PNG to LZ %ibit!\n", i_bpp );
							exit( 1 );
						}
					} else len0 = copy_data( fp_inp, fp_out, -1 );
					// update item header with correct size
					fseek( fp_inp, pos0 + 0x08, SEEK_SET );
					write_int32( fp_inp, len0 );
					// update RBM header
					len0 += 0x28;
					fseek( fp_inp, 20 + (16*i), SEEK_SET );
					write_int32( fp_inp, pos0 );
					write_int32( fp_inp, len0 );
				} else {
					fprintf( stderr, "ERROR\n" );
					fprintf( stderr, "file in list (%s) does not exist!\n", fn );
					exit( 1 );
				}			
				fclose( fp_out );
			}
			fprintf( stdout, "%i byte (%i%%)\n", len0,
				( i_hor*i_ver == 0 ) ? 0 : (100*len0) / (4*i_hor*i_ver) );
			i++;
		}
	}
	
	fclose( fp_inp ); fclose( fp_lst ); remove( TEMP_RGBA );
	fprintf( stdout, "-> done, %i file(s) %s in file \"%s\"\n", nf, 
		( dass ) ? "found" : "inserted", argv[2] );
	
	
	#if defined PAUSE
		// pause before exit
		fprintf( stdout, "\n\n" );
		fprintf( stdout, "< press ENTER >\n" );
		fgetc( stdin );
	#endif
	
	
	return 0;
}
