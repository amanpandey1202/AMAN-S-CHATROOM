import sys

def xor_encrypt(text, key=0x5E):
    bytes_arr = [ord(c) ^ key for c in text]
    hex_str = ", ".join([f"0x{b:02X}" for b in bytes_arr])
    return f"{{ {hex_str} }}"

if __name__ == "__main__":
    print("Purple Chatroom XOR Credentials Generator")
    print("========================================")
    key = 0x5E
    while True:
        try:
            text = input("Enter text to encrypt (or press Enter to quit): ").strip()
            if not text:
                break
            print(f"XOR Encrypted array (Key = 0x{key:02X}):")
            print(xor_encrypt(text, key))
            print()
        except KeyboardInterrupt:
            print("\nExiting...")
            break
