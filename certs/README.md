# SSL Certificates for Development

This directory contains self-signed SSL certificates for local development purposes.

## Generating Self-Signed Certificates

To generate self-signed certificates for development, run the following commands:

```bash
mkdir -p certs
cd certs
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes
```

When prompted, you can enter your development information or press Enter to accept defaults.

## Important Notes

1. These certificates are for development only and will show as "Not Secure" in browsers
2. In production, use proper certificates from a trusted authority
3. Never commit actual certificate files to public repositories

## Certificate Files

- `key.pem` - Private key file
- `cert.pem` - Certificate file

Both files should be placed in this directory but excluded from git using .gitignore
