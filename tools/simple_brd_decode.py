#!/usr/bin/env python3
"""
Simple BRD File Decoder
Basic implementation of BRD file decryption algorithm

Usage: python simple_brd_decode.py input.brd [output.brd]
"""

import sys
import os

def is_brd_encoded(data):
    """Check if file has BRD encoded signature: 0x23 0xE2 0x63 0x28"""
    if len(data) < 4:
        return False
    return data[0] == 0x23 and data[1] == 0xE2 and data[2] == 0x63 and data[3] == 0x28

def decode_brd_byte(byte_val):
    """
    Decode single byte using BRD algorithm from BRDFile.cpp:
    x = ~(((c >> 6) & 3) | (c << 2))
    Skip CR, LF, and NULL bytes
    """
    # Don't decode special characters
    if byte_val in (0x0D, 0x0A, 0x00):  # CR, LF, NULL
        return byte_val
    
    # BRD decoding algorithm
    high_bits = (byte_val >> 6) & 0x03     # Get top 2 bits
    shifted = (byte_val << 2) & 0xFF       # Shift left by 2
    combined = high_bits | shifted          # OR them together
    decoded = (~combined) & 0xFF           # Bitwise NOT
    
    return decoded

def decode_brd_file(input_file, output_file):
    """Decode BRD file"""
    # Read input file
    try:
        with open(input_file, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{input_file}' not found")
        return False
    except Exception as e:
        print(f"Error reading file: {e}")
        return False
    
    print(f"Input file: {input_file}")
    print(f"File size: {len(data)} bytes")
    
    # Check if encoded
    if not is_brd_encoded(data):
        print("Warning: File doesn't have BRD encoded signature")
        print("File may already be decoded or in plain text format")
        
        # Copy file unchanged
        try:
            with open(output_file, 'wb') as f:
                f.write(data)
            print(f"File copied unchanged to: {output_file}")
        except Exception as e:
            print(f"Error writing file: {e}")
            return False
        return True
    
    print("BRD encoded signature detected - decoding...")
    
    # Decode the file
    decoded_data = bytearray()
    for byte in data:
        decoded_byte = decode_brd_byte(byte)
        decoded_data.append(decoded_byte)
    
    # Write output file
    try:
        with open(output_file, 'wb') as f:
            f.write(decoded_data)
    except Exception as e:
        print(f"Error writing output file: {e}")
        return False
    
    print(f"Decoded file written to: {output_file}")
    
    # Try to analyze the decoded content
    try:
        text_content = decoded_data.decode('utf-8', errors='ignore')
        
        # Look for BRD sections
        brd_sections = ['var_data:', 'Format:', 'Parts:', 'Pins:', 'Nails:']
        found_sections = [section for section in brd_sections if section in text_content]
        
        if found_sections:
            print(f"BRD sections found: {', '.join(found_sections)}")
            print("Decoding appears successful!")
        else:
            print("Warning: No BRD sections found in decoded content")
            print("Decoding may have failed or file may not be BRD format")
    
    except Exception:
        print("Decoded content is not readable text")
    
    return True

def main():
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print("Usage: python simple_brd_decode.py input.brd [output.brd]")
        print("")
        print("Examples:")
        print("  python simple_brd_decode.py encoded.brd decoded.brd")
        print("  python simple_brd_decode.py test.brd")
        sys.exit(1)
    
    input_file = sys.argv[1]
    
    # Generate output filename if not provided
    if len(sys.argv) == 3:
        output_file = sys.argv[2]
    else:
        # Add _decoded suffix
        name, ext = os.path.splitext(input_file)
        output_file = f"{name}_decoded{ext}"
    
    success = decode_brd_file(input_file, output_file)
    
    if success:
        print("\nDecoding completed successfully!")
    else:
        print("\nDecoding failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
