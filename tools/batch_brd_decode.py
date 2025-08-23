#!/usr/bin/env python3
"""
BRD Batch Decoder
Process multiple BRD files in batch mode

Usage:
    python batch_brd_decode.py directory/
    python batch_brd_decode.py *.brd
    python batch_brd_decode.py file1.brd file2.brd file3.brd
"""

import sys
import os
import glob
from pathlib import Path
import argparse
import time

class BatchBRDDecoder:
    def __init__(self):
        self.BRD_SIGNATURE = bytes([0x23, 0xE2, 0x63, 0x28])
        self.stats = {
            'total_files': 0,
            'encoded_files': 0,
            'decoded_files': 0,
            'errors': 0,
            'skipped': 0
        }
    
    def is_brd_encoded(self, data):
        """Check if file has BRD encoded signature"""
        return len(data) >= 4 and data[:4] == self.BRD_SIGNATURE
    
    def decode_brd_byte(self, byte_val):
        """Decode single byte using BRD algorithm"""
        if byte_val in (0x0D, 0x0A, 0x00):  # Skip CR, LF, NULL
            return byte_val
        
        high_bits = (byte_val >> 6) & 0x03
        shifted = (byte_val << 2) & 0xFF
        combined = high_bits | shifted
        decoded = (~combined) & 0xFF
        
        return decoded
    
    def decode_file(self, input_path, output_path, overwrite=False):
        """Decode a single file"""
        input_path = Path(input_path)
        output_path = Path(output_path)
        
        # Check if output exists
        if output_path.exists() and not overwrite:
            print(f"  SKIP: Output file already exists: {output_path.name}")
            self.stats['skipped'] += 1
            return False
        
        try:
            # Read input file
            with open(input_path, 'rb') as f:
                data = f.read()
            
            # Check if encoded
            if not self.is_brd_encoded(data):
                print(f"  SKIP: Not encoded: {input_path.name}")
                self.stats['skipped'] += 1
                return False
            
            self.stats['encoded_files'] += 1
            
            # Decode the file
            decoded_data = bytearray()
            for byte in data:
                decoded_data.append(self.decode_brd_byte(byte))
            
            # Write output file
            with open(output_path, 'wb') as f:
                f.write(decoded_data)
            
            print(f"  DONE: {input_path.name} -> {output_path.name}")
            self.stats['decoded_files'] += 1
            return True
            
        except Exception as e:
            print(f"  ERROR: {input_path.name} - {str(e)}")
            self.stats['errors'] += 1
            return False
    
    def process_directory(self, directory, output_dir=None, suffix="_decoded", overwrite=False):
        """Process all BRD files in a directory"""
        directory = Path(directory)
        
        if not directory.exists():
            print(f"Error: Directory not found: {directory}")
            return False
        
        if output_dir:
            output_dir = Path(output_dir)
            output_dir.mkdir(exist_ok=True)
        else:
            output_dir = directory
        
        # Find BRD files
        brd_files = list(directory.glob("*.brd"))
        
        if not brd_files:
            print(f"No BRD files found in: {directory}")
            return False
        
        print(f"Found {len(brd_files)} BRD files in: {directory}")
        print(f"Output directory: {output_dir}")
        print()
        
        # Process each file
        for input_file in brd_files:
            self.stats['total_files'] += 1
            
            # Generate output filename
            output_file = output_dir / f"{input_file.stem}{suffix}{input_file.suffix}"
            
            print(f"Processing: {input_file.name}")
            self.decode_file(input_file, output_file, overwrite)
        
        return True
    
    def process_files(self, file_list, output_dir=None, suffix="_decoded", overwrite=False):
        """Process a list of files"""
        if output_dir:
            output_dir = Path(output_dir)
            output_dir.mkdir(exist_ok=True)
        
        print(f"Processing {len(file_list)} files...")
        if output_dir:
            print(f"Output directory: {output_dir}")
        print()
        
        for input_file in file_list:
            input_path = Path(input_file)
            self.stats['total_files'] += 1
            
            if not input_path.exists():
                print(f"  ERROR: File not found: {input_file}")
                self.stats['errors'] += 1
                continue
            
            # Generate output filename
            if output_dir:
                output_path = output_dir / f"{input_path.stem}{suffix}{input_path.suffix}"
            else:
                output_path = input_path.with_name(f"{input_path.stem}{suffix}{input_path.suffix}")
            
            print(f"Processing: {input_path.name}")
            self.decode_file(input_path, output_path, overwrite)
    
    def print_stats(self):
        """Print processing statistics"""
        print("\n" + "="*50)
        print("BATCH PROCESSING COMPLETE")
        print("="*50)
        print(f"Total files processed: {self.stats['total_files']}")
        print(f"Encoded files found:   {self.stats['encoded_files']}")
        print(f"Successfully decoded:  {self.stats['decoded_files']}")
        print(f"Skipped files:         {self.stats['skipped']}")
        print(f"Errors:                {self.stats['errors']}")
        
        if self.stats['total_files'] > 0:
            success_rate = (self.stats['decoded_files'] / self.stats['total_files']) * 100
            print(f"Success rate:          {success_rate:.1f}%")

def main():
    parser = argparse.ArgumentParser(
        description="Batch BRD File Decoder",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python batch_brd_decode.py /path/to/brd/files/           # Process directory
  python batch_brd_decode.py *.brd                         # Process wildcards
  python batch_brd_decode.py file1.brd file2.brd          # Process specific files
  python batch_brd_decode.py -o output/ *.brd             # Specify output directory
  python batch_brd_decode.py --suffix _dec *.brd          # Custom suffix
  python batch_brd_decode.py --overwrite *.brd            # Overwrite existing files
        """
    )
    
    parser.add_argument('inputs', nargs='+', help='Input files, directories, or patterns')
    parser.add_argument('-o', '--output', help='Output directory')
    parser.add_argument('-s', '--suffix', default='_decoded', help='Suffix for output files (default: _decoded)')
    parser.add_argument('--overwrite', action='store_true', help='Overwrite existing output files')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    decoder = BatchBRDDecoder()
    
    # Collect all input files
    all_files = []
    directories = []
    
    for input_item in args.inputs:
        # Expand wildcards
        if '*' in input_item or '?' in input_item:
            expanded = glob.glob(input_item)
            if expanded:
                all_files.extend(expanded)
            else:
                print(f"Warning: No files found matching pattern: {input_item}")
        else:
            path = Path(input_item)
            if path.is_dir():
                directories.append(path)
            elif path.is_file():
                all_files.append(str(path))
            else:
                print(f"Warning: Not found: {input_item}")
    
    start_time = time.time()
    
    # Process directories
    for directory in directories:
        decoder.process_directory(directory, args.output, args.suffix, args.overwrite)
    
    # Process individual files
    if all_files:
        decoder.process_files(all_files, args.output, args.suffix, args.overwrite)
    
    # Print results
    end_time = time.time()
    decoder.print_stats()
    print(f"Processing time:       {end_time - start_time:.2f} seconds")
    
    # Exit with error code if there were errors
    if decoder.stats['errors'] > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()
