#!/usr/bin/env python3
"""
BRD File Decryption Tool
Decodes XOR-encoded BRD files using the algorithm from BRDFile.cpp

Usage:
    python brd_decrypt.py input.brd output.brd
    python brd_decrypt.py --check input.brd
    python brd_decrypt.py --batch *.brd
"""

import sys
import os
import argparse
import glob
from pathlib import Path

class BRDDecryptor:
    """BRD file decryption class implementing the algorithm from BRDFile.cpp"""
    
    # BRD encoded signature: 0x23 0xE2 0x63 0x28
    ENCODED_SIGNATURE = bytes([0x23, 0xE2, 0x63, 0x28])
    
    def __init__(self):
        self.verbose = False
    
    def is_encoded(self, data):
        """Check if the file starts with BRD encoded signature"""
        if len(data) < 4:
            return False
        return data[:4] == self.ENCODED_SIGNATURE
    
    def decode_byte(self, byte_val):
        """
        Decode a single byte using BRD algorithm
        Algorithm: x = ~(((c >> 6) & 3) | (c << 2))
        Skip CR (0x0D), LF (0x0A), and NULL (0x00)
        """
        # Skip carriage return, line feed, and null bytes
        if byte_val in (0x0D, 0x0A, 0x00):
            return byte_val
        
        # Apply BRD decoding algorithm
        high_bits = (byte_val >> 6) & 0x03  # Extract top 2 bits
        shifted_left = (byte_val << 2) & 0xFF  # Shift left by 2, mask to byte
        combined = high_bits | shifted_left
        decoded = (~combined) & 0xFF  # Bitwise NOT, mask to byte
        
        return decoded
    
    def decode_file(self, input_data):
        """Decode entire file data"""
        if not self.is_encoded(input_data):
            if self.verbose:
                print("File is not encoded (no BRD signature found)")
            return input_data  # Return unchanged if not encoded
        
        if self.verbose:
            print("BRD encoded signature detected, decoding...")
        
        decoded_data = bytearray()
        
        for byte_val in input_data:
            decoded_byte = self.decode_byte(byte_val)
            decoded_data.append(decoded_byte)
        
        return bytes(decoded_data)
    
    def analyze_decoded_content(self, decoded_data):
        """Analyze decoded content to verify successful decoding"""
        try:
            # Try to decode as text for analysis
            text_content = decoded_data.decode('utf-8', errors='ignore')
            
            # Look for common BRD sections
            sections_found = []
            common_sections = [
                'var_data:', 'Format:', 'format:', 'Parts:', 'Pins1:',
                'Pins:', 'Pins2:', 'Nails:', 'OUTLINE:', 'NETS:', 'BRDOUT:'
            ]
            
            for section in common_sections:
                if section in text_content:
                    sections_found.append(section)
            
            # Count printable characters
            printable_chars = sum(1 for c in text_content if c.isprintable() or c in '\r\n\t')
            printable_ratio = printable_chars / len(text_content) if text_content else 0
            
            return {
                'sections_found': sections_found,
                'printable_ratio': printable_ratio,
                'line_count': text_content.count('\n'),
                'appears_valid': len(sections_found) > 0 and printable_ratio > 0.7
            }
        except Exception as e:
            return {
                'sections_found': [],
                'printable_ratio': 0,
                'line_count': 0,
                'appears_valid': False,
                'error': str(e)
            }
    
    def decrypt_file(self, input_path, output_path=None):
        """Decrypt a single BRD file"""
        input_path = Path(input_path)
        
        if not input_path.exists():
            raise FileNotFoundError(f"Input file not found: {input_path}")
        
        # Read input file
        with open(input_path, 'rb') as f:
            input_data = f.read()
        
        if self.verbose:
            print(f"Processing: {input_path}")
            print(f"File size: {len(input_data)} bytes")
        
        # Check if file is encoded
        if not self.is_encoded(input_data):
            print(f"Warning: {input_path} doesn't appear to be encoded (no BRD signature)")
            if output_path:
                print("Copying file unchanged...")
                with open(output_path, 'wb') as f:
                    f.write(input_data)
            return False
        
        # Decode the file
        decoded_data = self.decode_file(input_data)
        
        # Analyze results
        analysis = self.analyze_decoded_content(decoded_data)
        
        if self.verbose:
            print(f"Decoding analysis:")
            print(f"  Sections found: {', '.join(analysis['sections_found']) if analysis['sections_found'] else 'None'}")
            print(f"  Printable ratio: {analysis['printable_ratio']:.2%}")
            print(f"  Line count: {analysis['line_count']}")
            print(f"  Appears valid: {analysis['appears_valid']}")
        
        # Write output file
        if output_path:
            output_path = Path(output_path)
            with open(output_path, 'wb') as f:
                f.write(decoded_data)
            print(f"Decoded file written to: {output_path}")
        
        return analysis['appears_valid']
    
    def check_file(self, file_path):
        """Check if a file is encoded and show analysis"""
        file_path = Path(file_path)
        
        if not file_path.exists():
            print(f"Error: File not found: {file_path}")
            return False
        
        with open(file_path, 'rb') as f:
            data = f.read()
        
        print(f"File: {file_path}")
        print(f"Size: {len(data)} bytes")
        
        if self.is_encoded(data):
            print("Status: ENCODED (BRD signature found)")
            print("Signature: 0x23 0xE2 0x63 0x28")
            
            # Try decoding and analyze
            decoded = self.decode_file(data)
            analysis = self.analyze_decoded_content(decoded)
            
            print(f"Decoding preview:")
            print(f"  BRD sections found: {len(analysis['sections_found'])}")
            if analysis['sections_found']:
                print(f"  Sections: {', '.join(analysis['sections_found'])}")
            print(f"  Printable content: {analysis['printable_ratio']:.1%}")
            print(f"  Estimated validity: {'GOOD' if analysis['appears_valid'] else 'QUESTIONABLE'}")
            
        else:
            print("Status: PLAIN TEXT (no encoding detected)")
            
            # Check if it looks like decoded BRD content
            try:
                text = data.decode('utf-8', errors='ignore')
                brd_keywords = ['var_data:', 'Format:', 'Parts:', 'Pins:', 'Nails:']
                found_keywords = [kw for kw in brd_keywords if kw in text]
                
                if found_keywords:
                    print(f"Content appears to be BRD format with sections: {', '.join(found_keywords)}")
                else:
                    print("Content does not appear to be BRD format")
            except:
                print("Content appears to be binary data")
        
        print()
        return True

def main():
    parser = argparse.ArgumentParser(
        description="BRD File Decryption Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python brd_decrypt.py input.brd output.brd    # Decrypt single file
  python brd_decrypt.py --check input.brd       # Check file status
  python brd_decrypt.py --batch *.brd           # Decrypt multiple files
  python brd_decrypt.py -v input.brd out.brd    # Verbose output
        """
    )
    
    parser.add_argument('input', nargs='?', help='Input BRD file(s)')
    parser.add_argument('output', nargs='?', help='Output file (for single file mode)')
    parser.add_argument('-c', '--check', action='store_true', 
                       help='Check file encoding status without decrypting')
    parser.add_argument('-b', '--batch', action='store_true',
                       help='Batch mode: process multiple files')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Verbose output')
    parser.add_argument('--suffix', default='_decoded',
                       help='Suffix for output files in batch mode (default: _decoded)')
    
    args = parser.parse_args()
    
    # Create decryptor instance
    decryptor = BRDDecryptor()
    decryptor.verbose = args.verbose
    
    # Handle different modes
    if args.check:
        if not args.input:
            print("Error: Input file required for check mode")
            sys.exit(1)
        
        # Handle wildcards for check mode
        files = glob.glob(args.input) if '*' in args.input else [args.input]
        
        for file_path in files:
            decryptor.check_file(file_path)
    
    elif args.batch:
        if not args.input:
            print("Error: Input pattern required for batch mode")
            sys.exit(1)
        
        # Expand wildcards
        files = glob.glob(args.input)
        
        if not files:
            print(f"Error: No files found matching pattern: {args.input}")
            sys.exit(1)
        
        print(f"Processing {len(files)} files...")
        success_count = 0
        
        for file_path in files:
            file_path = Path(file_path)
            output_path = file_path.with_name(f"{file_path.stem}{args.suffix}{file_path.suffix}")
            
            try:
                if decryptor.decrypt_file(file_path, output_path):
                    success_count += 1
            except Exception as e:
                print(f"Error processing {file_path}: {e}")
        
        print(f"\nBatch complete: {success_count}/{len(files)} files successfully processed")
    
    else:
        # Single file mode
        if not args.input:
            print("Error: Input file required")
            parser.print_help()
            sys.exit(1)
        
        if not args.output:
            # Generate output filename
            input_path = Path(args.input)
            args.output = input_path.with_name(f"{input_path.stem}_decoded{input_path.suffix}")
        
        try:
            decryptor.decrypt_file(args.input, args.output)
        except Exception as e:
            print(f"Error: {e}")
            sys.exit(1)

if __name__ == "__main__":
    main()
