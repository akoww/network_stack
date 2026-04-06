#!/usr/bin/env python3
"""
TLS Certificate Generation Tool

Generates certificates for testing TLS server/client functionality.

Usage:
    python generate_certs.py [--output-dir DIR] [--common-name NAME] [--days DAYS]

Outputs:
    - ca.crt          : CA certificate
    - ca.key          : CA private key
    - server.crt      : Server certificate
    - server.key      : Server private key
    - client.crt      : Client certificate (optional)
    - client.key      : Client private key (optional)
"""

import argparse
import subprocess
import os
from pathlib import Path


def run_cmd(*args):
    """Run a command and raise on failure."""
    result = subprocess.run(
        args, capture_output=True, text=True, check=True
    )
    return result.stdout


def generate_ca(output_dir: Path) -> None:
    """Generate CA certificate and key."""
    print("Generating CA certificate...")
    
    # Generate CA private key
    run_cmd(
        "openssl", "genrsa", "-out", str(output_dir / "ca.key"), "2048"
    )
    
    # Generate CA certificate (self-signed)
    run_cmd(
        "openssl", "req", "-x509", "-new", "-nodes",
        "-key", str(output_dir / "ca.key"),
        "-sha256", "-days", "365",
        "-out", str(output_dir / "ca.crt"),
        "-subj", "/C=US/ST=Test/L=Test/O=Test/CN=TestCA"
    )
    print("CA certificate generated: ca.crt, ca.key")


def generate_server_cert(
    output_dir: Path, common_name: str, ca_cert: Path, ca_key: Path
) -> None:
    """Generate server certificate signed by CA."""
    print(f"Generating server certificate (CN={common_name})...")
    
    # Generate server private key
    run_cmd(
        "openssl", "genrsa", "-out", str(output_dir / "server.key"), "2048"
    )
    
    # Generate server CSR
    run_cmd(
        "openssl", "req", "-new",
        "-key", str(output_dir / "server.key"),
        "-out", str(output_dir / "server.csr"),
        "-subj", f"/C=US/ST=Test/L=Test/O=Test/CN={common_name}"
    )
    
    # Sign server certificate with CA
    run_cmd(
        "openssl", "x509", "-req",
        "-in", str(output_dir / "server.csr"),
        "-CA", str(ca_cert),
        "-CAkey", str(ca_key),
        "-CAcreateserial",
        "-out", str(output_dir / "server.crt"),
        "-days", "365",
        "-sha256"
    )
    
    # Cleanup CSR
    (output_dir / "server.csr").unlink()
    
    print("Server certificate generated: server.crt, server.key")


def generate_client_cert(
    output_dir: Path, ca_cert: Path, ca_key: Path
) -> None:
    """Generate client certificate signed by CA."""
    print("Generating client certificate...")
    
    # Generate client private key
    run_cmd(
        "openssl", "genrsa", "-out", str(output_dir / "client.key"), "2048"
    )
    
    # Generate client CSR
    run_cmd(
        "openssl", "req", "-new",
        "-key", str(output_dir / "client.key"),
        "-out", str(output_dir / "client.csr"),
        "-subj", "/C=US/ST=Test/L=Test/O=Test/CN=test-client"
    )
    
    # Sign client certificate with CA
    run_cmd(
        "openssl", "x509", "-req",
        "-in", str(output_dir / "client.csr"),
        "-CA", str(ca_cert),
        "-CAkey", str(ca_key),
        "-CAcreateserial",
        "-out", str(output_dir / "client.crt"),
        "-days", "365",
        "-sha256"
    )
    
    # Cleanup CSR
    (output_dir / "client.csr").unlink()
    
    print("Client certificate generated: client.crt, client.key")


def main():
    parser = argparse.ArgumentParser(
        description="Generate TLS certificates for unit testing"
    )
    parser.add_argument(
        "--output-dir", "-o",
        type=Path,
        default=Path("tests/certs"),
        help="Output directory for generated certificates (default: tests/certs)"
    )
    parser.add_argument(
        "--common-name", "-n",
        default="localhost",
        help="Common name for server certificate (default: localhost)"
    )
    parser.add_argument(
        "--days", "-d",
        type=int,
        default=365,
        help="Certificate validity in days (default: 365)"
    )
    parser.add_argument(
        "--no-client",
        action="store_true",
        help="Skip client certificate generation"
    )
    
    args = parser.parse_args()
    
    # Create output directory
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Verify OpenSSL is available
    try:
        run_cmd("openssl", "version")
    except subprocess.CalledProcessError:
        print("ERROR: OpenSSL is not installed or not in PATH")
        return 1
    
    print(f"Generating certificates to: {output_dir}")
    
    # Generate CA
    generate_ca(output_dir)
    
    # Generate server certificate
    generate_server_cert(
        output_dir, args.common_name,
        output_dir / "ca.crt", output_dir / "ca.key"
    )
    
    # Generate client certificate (optional)
    if not args.no_client:
        generate_client_cert(
            output_dir,
            output_dir / "ca.crt", output_dir / "ca.key"
        )
    
    print("\nDone! Certificates generated:")
    for f in sorted(output_dir.glob("*.crt")):
        print(f"  - {f.name}")
    for f in sorted(output_dir.glob("*.key")):
        print(f"  - {f.name}")
    
    return 0


if __name__ == "__main__":
    exit(main())
