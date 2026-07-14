#!/usr/bin/env python3
import sys
import subprocess
from pathlib import Path

def main():
    repo_root = Path(__file__).resolve().parent.parent.parent
    protocol_dir = repo_root / 'protocol'
    
    print("Verifying canonical protocol generation...")
    
    # Run the protocol tool verify command
    try:
        subprocess.run(
            [sys.executable, str(protocol_dir / 'tools' / 'protocol.py'), '--verify'],
            cwd=str(repo_root),
            check=True
        )
    except subprocess.CalledProcessError:
        print("\nERROR: Protocol metadata verification failed.")
        print("The generated code does not match the YAML contracts in protocol/contracts/.")
        print("Run `python protocol/tools/protocol.py` to regenerate the artifacts.")
        sys.exit(1)
        
    print("Protocol verified successfully.")
    sys.exit(0)

if __name__ == '__main__':
    main()
