#!/usr/bin/env python3
"""keygen.py -- make the key pair that signs Onyx's package index (docs/pkg/README.md), once.

    python3 tools/pkg/keygen.py [~/.onyx/pkg-key.pem]

The private key (ECDSA P-256, PEM) goes to the path given -- keep it OFF the repositories: whoever
has it can publish packages every Onyx installs. The public half goes to sdcard/etc/pkg/onyx.pub,
which the system package carries to the cards (pkg checks index.sig with it).
"""
import os, sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (os.path.dirname (HERE))
priv = os.path.expanduser (sys.argv[1] if len (sys.argv) > 1 else "~/.onyx/pkg-key.pem")
pub = sys.argv[2] if len (sys.argv) > 2 else os.path.join (ROOT, "sdcard", "etc", "pkg", "onyx.pub")
if os.path.exists (priv): sys.exit ("keygen: %s exists already (a new key would refuse every card's index)" % priv)
k = ec.generate_private_key (ec.SECP256R1 ())
os.makedirs (os.path.dirname (priv) or ".", exist_ok = True)
with open (priv, "wb") as f:
	f.write (k.private_bytes (serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption ()))
os.chmod (priv, 0o600)
os.makedirs (os.path.dirname (pub), exist_ok = True)
with open (pub, "wb") as f:
	f.write (k.public_key ().public_bytes (serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
print ("keygen: private key", priv, "(keep it safe), public key", pub)
