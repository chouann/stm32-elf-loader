#!/usr/bin/env python3
"""
Script to clean up UART output files by removing redundant whitespace
Keeps only useful data without excessive padding
"""

import os
import sys
from pathlib import Path

def extract_hexdump(input_file, output_file=None):
    """
    Extract hexdump section from UART output file and save to separate file
    Looks for section between "=== Hexdump:" and "=== End Hexdump ==="
    """
    
    if output_file is None:
        # Default output to STM32_ver/stm32_dump.txt
        output_file = Path(input_file).parent / "STM32_ver" / "stm32_dump.txt"
    
    output_file = Path(output_file)
    output_file.parent.mkdir(parents=True, exist_ok=True)
    
    print(f"Extracting hexdump from: {input_file}")
    
    try:
        # Read input file
        with open(input_file, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
        
        hexdump_lines = []
        in_hexdump = False
        
        for line in lines:
            # Check for hexdump start
            if "=== Hexdump:" in line:
                in_hexdump = True
                hexdump_lines.append(line)
            # Check for hexdump end
            elif "=== End Hexdump ===" in line:
                hexdump_lines.append(line)
                in_hexdump = False
            # Collect hexdump content
            elif in_hexdump:
                hexdump_lines.append(line)
        
        if not hexdump_lines:
            print(f"  ✗ No hexdump section found in {input_file}")
            return False
        
        # Write hexdump to output file
        with open(output_file, 'w', encoding='utf-8') as f:
            f.writelines(hexdump_lines)
        
        print(f"  ✓ Hexdump extracted to: {output_file}")
        print(f"  Lines extracted: {len(hexdump_lines)}")
        
        return True
        
    except Exception as e:
        print(f"  ✗ Error extracting hexdump: {e}")
        return False

def clean_uart_output(input_file, output_file=None):
    """
    Clean UART output file by:
    1. Stripping leading/trailing whitespace from each line
    2. Removing consecutive blank lines (max 1 blank line between content)
    3. Removing trailing whitespace at end of file
    """
    
    if output_file is None:
        output_file = input_file.replace('.txt', '_cleaned.txt')
    
    print(f"Cleaning: {input_file}")
    
    try:
        # Read the file
        with open(input_file, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
        
        # Process lines
        cleaned_lines = []
        prev_blank = False
        
        for line in lines:
            # Strip leading/trailing whitespace
            stripped = line.strip()
            
            # Check if line is blank or only whitespace
            if not stripped or stripped.isspace():
                if not prev_blank:
                    cleaned_lines.append('\n')
                    prev_blank = True
            else:
                cleaned_lines.append(stripped + '\n')
                prev_blank = False
        
        # Remove trailing blank lines
        while cleaned_lines and (cleaned_lines[-1] == '\n' or cleaned_lines[-1].isspace()):
            cleaned_lines.pop()
        
        # Add final newline
        if cleaned_lines:
            cleaned_lines.append('\n')
        
        # Write cleaned output
        with open(output_file, 'w', encoding='utf-8') as f:
            f.writelines(cleaned_lines)
        
        print(f"  ✓ Cleaned output saved to: {output_file}")
        
        # Show statistics
        original_lines = len(lines)
        cleaned_count = len(cleaned_lines)
        reduction = ((original_lines - cleaned_count) / original_lines * 100) if original_lines > 0 else 0
        print(f"  Original lines: {original_lines}, Cleaned lines: {cleaned_count} ({reduction:.1f}% reduction)")
        
        return True
        
    except Exception as e:
        print(f"  ✗ Error processing {input_file}: {e}")
        return False

def process_folder(folder_path):
    """
    [DEPRECATED] Process all .txt files in the uart_output folder
    Use direct file specification instead: python clean_uart_output.py <file.txt>
    """
    pass


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python clean_uart_output.py <input_file.txt> [output_file.txt] [--hexdump-only]")
        print("  <input_file.txt>   : Path to the UART output file to clean (required)")
        print("  [output_file.txt]  : Path for cleaned output (optional, defaults to input_file_cleaned.txt)")
        print("  [--hexdump-only]   : Extract only hexdump section to STM32_ver/stm32_dump.txt")
        print("\nExamples:")
        print("  python clean_uart_output.py testRead.txt")
        print("  python clean_uart_output.py testRead.txt testRead_fixed.txt")
        print("  python clean_uart_output.py testRead.txt --hexdump-only")
        sys.exit(1)
    
    input_file = sys.argv[1]
    
    if not Path(input_file).exists():
        print(f"Error: File '{input_file}' does not exist")
        sys.exit(1)
    
    print(f"\n=== UART Output Processing ===\n")
    
    # Check if hexdump-only flag is set
    if "--hexdump-only" in sys.argv:
        # First clean the file, then extract hexdump from cleaned version
        cleaned_file = input_file.replace('.txt', '_cleaned.txt')
        clean_uart_output(input_file, cleaned_file)
        print()  # Blank line for readability
        extract_hexdump(cleaned_file)
    else:
        output_file = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else None
        clean_uart_output(input_file, output_file)
    
    print(f"\n=== Processing Complete ===\n")
