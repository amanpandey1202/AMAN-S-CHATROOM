import sys

with open('node_a/node_a.ino', 'r', encoding='utf-8', errors='ignore') as f:
    a_lines = f.readlines()

with open('node_b/node_b.ino', 'r', encoding='utf-8', errors='ignore') as f:
    b_lines = f.readlines()

print(f"node_a lines: {len(a_lines)}")
print(f"node_b lines: {len(b_lines)}")

# Compare lines
diff_count = 0
for i in range(min(len(a_lines), len(b_lines))):
    la = a_lines[i].strip()
    lb = b_lines[i].strip()
    if la != lb:
        # Ignore expected differences
        if any(keyword in la for keyword in ['NODE_ID', 'RADIO_PIPE', 'Node A', 'Node B', 'node_a', 'node_b', 'AMAN\'S CHATROOM - A', 'AMAN\'S CHATROOM - B']):
            continue
        print(f"Line {i+1}:")
        print(f"  A: {la[:100]}")
        print(f"  B: {lb[:100]}")
        diff_count += 1
        if diff_count >= 25:
            print("... and more diffs")
            break

if diff_count == 0:
    print("✅ No unexpected differences found! Node A and Node B logic is 100% synchronized!")
