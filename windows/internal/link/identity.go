// 节点身份：自签名证书 + 指纹（§8.1 TOFU）。
//
// 首跑生成 ECDSA P-256 自签证书并持久化到数据目录；指纹是
// hex(SHA-256(证书 DER)[0:16])，与 §4.3 的 ECDH 公钥指纹（BLE/Raw Ethernet
// 配对路径）不是一个东西——TLS 路径按 §8.1「信任证书指纹」。
package link

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"io/fs"
	"math/big"
	"os"
	"path/filepath"
	"time"
)

type Identity struct {
	Cert        tls.Certificate
	Fingerprint string // 本机证书指纹（32 个十六进制字符）
}

// LoadOrCreate 加载（或首跑生成并写入）数据目录下的 core.pem（证书+私钥同文件）。
func LoadOrCreate(dataDir string) (*Identity, error) {
	path := filepath.Join(dataDir, "core.pem")
	if data, err := os.ReadFile(path); err == nil {
		cert, err := tls.X509KeyPair(data, data)
		if err != nil {
			return nil, fmt.Errorf("link: parse %s: %w", path, err)
		}
		return newIdentity(cert), nil
	} else if !errors.Is(err, fs.ErrNotExist) {
		return nil, fmt.Errorf("link: read %s: %w", path, err)
	}

	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return nil, fmt.Errorf("link: gen key: %w", err)
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 120))
	if err != nil {
		return nil, err
	}
	tpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "flowgrid"},
		DNSNames:              []string{"flowgrid.local"},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().AddDate(10, 0, 0),
		KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth, x509.ExtKeyUsageClientAuth},
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &key.PublicKey, key)
	if err != nil {
		return nil, fmt.Errorf("link: create cert: %w", err)
	}
	keyDER, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		return nil, err
	}
	buf := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})
	buf = append(buf, pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDER})...)
	if err := os.MkdirAll(dataDir, 0o700); err != nil {
		return nil, err
	}
	if err := os.WriteFile(path, buf, 0o600); err != nil {
		return nil, fmt.Errorf("link: write %s: %w", path, err)
	}
	cert, err := tls.X509KeyPair(buf, buf)
	if err != nil {
		return nil, err
	}
	return newIdentity(cert), nil
}

func newIdentity(cert tls.Certificate) *Identity {
	// tls.Certificate.Leaf 可能为 nil（惰性解析），指纹按 DER 显式解析。
	leaf, err := x509.ParseCertificate(cert.Certificate[0])
	if err != nil {
		panic(fmt.Sprintf("link: parse own certificate: %v", err))
	}
	return &Identity{Cert: cert, Fingerprint: Fingerprint(leaf.Raw)}
}

// Fingerprint 按本变更约定计算证书指纹（§8.1 TOFU）。
func Fingerprint(der []byte) string {
	sum := sha256.Sum256(der)
	return hex.EncodeToString(sum[:16])
}
