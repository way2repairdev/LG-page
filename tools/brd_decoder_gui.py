#!/usr/bin/env python3
"""
BRD File Decoder GUI
Graphical interface for BRD file decryption

Requirements: tkinter (usually included with Python)
Usage: python brd_decoder_gui.py
"""

import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk
import os
from pathlib import Path
import threading

class BRDDecoderGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("BRD File Decoder")
        self.root.geometry("600x500")
        
        # BRD signature
        self.BRD_SIGNATURE = bytes([0x23, 0xE2, 0x63, 0x28])
        
        self.setup_ui()
    
    def setup_ui(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # File selection
        file_frame = ttk.LabelFrame(main_frame, text="File Selection", padding="5")
        file_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        
        ttk.Label(file_frame, text="Input File:").grid(row=0, column=0, sticky=tk.W)
        self.input_var = tk.StringVar()
        ttk.Entry(file_frame, textvariable=self.input_var, width=50).grid(row=0, column=1, padx=(5, 5))
        ttk.Button(file_frame, text="Browse", command=self.browse_input).grid(row=0, column=2)
        
        ttk.Label(file_frame, text="Output File:").grid(row=1, column=0, sticky=tk.W, pady=(5, 0))
        self.output_var = tk.StringVar()
        ttk.Entry(file_frame, textvariable=self.output_var, width=50).grid(row=1, column=1, padx=(5, 5), pady=(5, 0))
        ttk.Button(file_frame, text="Browse", command=self.browse_output).grid(row=1, column=2, pady=(5, 0))
        
        # Auto-generate output filename checkbox
        self.auto_output = tk.BooleanVar(value=True)
        ttk.Checkbutton(file_frame, text="Auto-generate output filename", 
                       variable=self.auto_output, command=self.on_auto_output_change).grid(row=2, column=1, sticky=tk.W, pady=(5, 0))
        
        # File info
        info_frame = ttk.LabelFrame(main_frame, text="File Information", padding="5")
        info_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        
        self.info_text = scrolledtext.ScrolledText(info_frame, height=6, width=70)
        self.info_text.grid(row=0, column=0, sticky=(tk.W, tk.E))
        
        # Buttons
        button_frame = ttk.Frame(main_frame)
        button_frame.grid(row=2, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        
        ttk.Button(button_frame, text="Check File", command=self.check_file).grid(row=0, column=0, padx=(0, 5))
        ttk.Button(button_frame, text="Decode File", command=self.decode_file).grid(row=0, column=1, padx=(0, 5))
        ttk.Button(button_frame, text="Clear Log", command=self.clear_log).grid(row=0, column=2, padx=(0, 5))
        
        # Progress bar
        self.progress = ttk.Progressbar(main_frame, mode='indeterminate')
        self.progress.grid(row=3, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        
        # Log output
        log_frame = ttk.LabelFrame(main_frame, text="Log Output", padding="5")
        log_frame.grid(row=4, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=10, width=70)
        self.log_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(0, weight=1)
        main_frame.rowconfigure(4, weight=1)
        file_frame.columnconfigure(1, weight=1)
        info_frame.columnconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
    
    def log(self, message):
        """Add message to log"""
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.root.update()
    
    def clear_log(self):
        """Clear log output"""
        self.log_text.delete(1.0, tk.END)
        self.info_text.delete(1.0, tk.END)
    
    def browse_input(self):
        """Browse for input file"""
        filename = filedialog.askopenfilename(
            title="Select BRD file to decode",
            filetypes=[("BRD files", "*.brd"), ("All files", "*.*")]
        )
        if filename:
            self.input_var.set(filename)
            self.update_output_filename()
    
    def browse_output(self):
        """Browse for output file"""
        filename = filedialog.asksaveasfilename(
            title="Save decoded file as",
            filetypes=[("BRD files", "*.brd"), ("All files", "*.*")]
        )
        if filename:
            self.output_var.set(filename)
            self.auto_output.set(False)
    
    def on_auto_output_change(self):
        """Handle auto-output checkbox change"""
        if self.auto_output.get():
            self.update_output_filename()
    
    def update_output_filename(self):
        """Update output filename based on input"""
        if self.auto_output.get() and self.input_var.get():
            input_path = Path(self.input_var.get())
            output_path = input_path.with_name(f"{input_path.stem}_decoded{input_path.suffix}")
            self.output_var.set(str(output_path))
    
    def is_brd_encoded(self, data):
        """Check if file has BRD encoded signature"""
        if len(data) < 4:
            return False
        return data[:4] == self.BRD_SIGNATURE
    
    def decode_brd_byte(self, byte_val):
        """Decode single byte using BRD algorithm"""
        if byte_val in (0x0D, 0x0A, 0x00):  # Skip CR, LF, NULL
            return byte_val
        
        high_bits = (byte_val >> 6) & 0x03
        shifted = (byte_val << 2) & 0xFF
        combined = high_bits | shifted
        decoded = (~combined) & 0xFF
        
        return decoded
    
    def analyze_file(self, file_path):
        """Analyze file and return information"""
        try:
            with open(file_path, 'rb') as f:
                data = f.read()
            
            file_size = len(data)
            is_encoded = self.is_brd_encoded(data)
            
            info = {
                'size': file_size,
                'is_encoded': is_encoded,
                'signature': data[:4].hex() if len(data) >= 4 else 'N/A'
            }
            
            if is_encoded:
                # Try decoding and analyze
                decoded_data = bytearray()
                for byte in data:
                    decoded_data.append(self.decode_brd_byte(byte))
                
                try:
                    text_content = decoded_data.decode('utf-8', errors='ignore')
                    brd_sections = ['var_data:', 'Format:', 'Parts:', 'Pins:', 'Nails:', 'OUTLINE:', 'NETS:']
                    found_sections = [section for section in brd_sections if section in text_content]
                    
                    info['sections_found'] = found_sections
                    info['line_count'] = text_content.count('\n')
                    info['appears_valid'] = len(found_sections) > 0
                except:
                    info['sections_found'] = []
                    info['appears_valid'] = False
            else:
                # Check if it's already decoded BRD content
                try:
                    text_content = data.decode('utf-8', errors='ignore')
                    brd_sections = ['var_data:', 'Format:', 'Parts:', 'Pins:', 'Nails:']
                    found_sections = [section for section in brd_sections if section in text_content]
                    info['sections_found'] = found_sections
                    info['already_decoded'] = len(found_sections) > 0
                except:
                    info['already_decoded'] = False
            
            return info
            
        except Exception as e:
            return {'error': str(e)}
    
    def check_file(self):
        """Check file and display information"""
        input_file = self.input_var.get()
        if not input_file:
            messagebox.showerror("Error", "Please select an input file")
            return
        
        if not os.path.exists(input_file):
            messagebox.showerror("Error", "Input file does not exist")
            return
        
        self.info_text.delete(1.0, tk.END)
        self.log("Checking file...")
        
        info = self.analyze_file(input_file)
        
        if 'error' in info:
            self.info_text.insert(tk.END, f"Error: {info['error']}")
            return
        
        # Display file information
        self.info_text.insert(tk.END, f"File: {os.path.basename(input_file)}\n")
        self.info_text.insert(tk.END, f"Size: {info['size']} bytes\n")
        self.info_text.insert(tk.END, f"Signature: {info['signature']}\n")
        
        if info['is_encoded']:
            self.info_text.insert(tk.END, "Status: ENCODED (BRD signature detected)\n")
            if 'sections_found' in info:
                self.info_text.insert(tk.END, f"Sections found after decoding: {', '.join(info['sections_found'])}\n")
                self.info_text.insert(tk.END, f"Decoding validity: {'GOOD' if info['appears_valid'] else 'QUESTIONABLE'}\n")
        else:
            self.info_text.insert(tk.END, "Status: PLAIN TEXT (no encoding detected)\n")
            if info.get('already_decoded'):
                self.info_text.insert(tk.END, "Content appears to be decoded BRD format\n")
        
        self.log("File check completed")
    
    def decode_file_worker(self):
        """Worker thread for file decoding"""
        try:
            input_file = self.input_var.get()
            output_file = self.output_var.get()
            
            # Read input file
            with open(input_file, 'rb') as f:
                data = f.read()
            
            self.log(f"Input file: {os.path.basename(input_file)}")
            self.log(f"File size: {len(data)} bytes")
            
            # Check if encoded
            if not self.is_brd_encoded(data):
                self.log("Warning: File doesn't have BRD encoded signature")
                self.log("Copying file unchanged...")
                
                with open(output_file, 'wb') as f:
                    f.write(data)
                
                self.log(f"File copied to: {os.path.basename(output_file)}")
                messagebox.showinfo("Complete", "File copied unchanged (no encoding detected)")
                return
            
            self.log("BRD encoded signature detected - decoding...")
            
            # Decode the file
            decoded_data = bytearray()
            for i, byte in enumerate(data):
                decoded_data.append(self.decode_brd_byte(byte))
                
                # Update progress occasionally
                if i % 1000 == 0:
                    self.root.after(0, lambda: None)  # Allow GUI updates
            
            # Write output file
            with open(output_file, 'wb') as f:
                f.write(decoded_data)
            
            self.log(f"Decoded file written to: {os.path.basename(output_file)}")
            
            # Analyze results
            try:
                text_content = decoded_data.decode('utf-8', errors='ignore')
                brd_sections = ['var_data:', 'Format:', 'Parts:', 'Pins:', 'Nails:']
                found_sections = [section for section in brd_sections if section in text_content]
                
                if found_sections:
                    self.log(f"BRD sections found: {', '.join(found_sections)}")
                    self.log("Decoding appears successful!")
                    messagebox.showinfo("Success", f"File decoded successfully!\nSections found: {', '.join(found_sections)}")
                else:
                    self.log("Warning: No BRD sections found in decoded content")
                    messagebox.showwarning("Warning", "Decoding completed but no BRD sections found")
            
            except Exception:
                self.log("Decoded content is not readable text")
                messagebox.showwarning("Warning", "File decoded but content is not readable text")
        
        except Exception as e:
            self.log(f"Error: {str(e)}")
            messagebox.showerror("Error", f"Decoding failed: {str(e)}")
        
        finally:
            # Stop progress bar and re-enable buttons
            self.root.after(0, self.decode_finished)
    
    def decode_finished(self):
        """Called when decoding is finished"""
        self.progress.stop()
        self.progress.grid_remove()
        
        # Re-enable buttons (if you disabled them)
        for widget in self.root.winfo_children():
            if isinstance(widget, ttk.Frame):
                for child in widget.winfo_children():
                    if isinstance(child, ttk.Button):
                        child.config(state='normal')
    
    def decode_file(self):
        """Start file decoding"""
        input_file = self.input_var.get()
        output_file = self.output_var.get()
        
        if not input_file:
            messagebox.showerror("Error", "Please select an input file")
            return
        
        if not output_file:
            messagebox.showerror("Error", "Please specify an output file")
            return
        
        if not os.path.exists(input_file):
            messagebox.showerror("Error", "Input file does not exist")
            return
        
        # Show progress bar
        self.progress.grid()
        self.progress.start()
        
        # Start decoding in a separate thread
        thread = threading.Thread(target=self.decode_file_worker)
        thread.daemon = True
        thread.start()

def main():
    root = tk.Tk()
    app = BRDDecoderGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()
