"""Create throwaway build keys only. Never use CI binaries on a real pair."""
from pathlib import Path
import secrets
root = Path(__file__).resolve().parents[1]
key = ', '.join(f'0x{x:02x}' for x in secrets.token_bytes(32))
for role in ('Emisor', 'Receptor'):
    path = root / role / 'pair_key.h'
    if path.exists():
        raise SystemExit(f'Refusing to overwrite {path}')
    path.write_text('#pragma once\nstatic const uint8_t PAIR_KEY[32] = {' + key + '};\n')
