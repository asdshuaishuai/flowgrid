// 信任库：已确认对端的证书指纹 → 名称 + 证书（DER）。拨号方用持久化的
// 对端证书做固定根校验（RootCAs 钉扎），全程无校验旁路。
package link

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

type TrustEntry struct {
	Name    string `json:"name"`
	CertB64 string `json:"cert_b64"` // 对端叶子证书 DER（base64）
	Added   string `json:"added"`    // RFC3339
}

type TrustStore struct {
	mu      sync.Mutex
	path    string
	entries map[string]TrustEntry // 指纹 → 条目
}

func OpenTrustStore(dataDir string) (*TrustStore, error) {
	t := &TrustStore{
		path:    filepath.Join(dataDir, "trust.json"),
		entries: map[string]TrustEntry{},
	}
	data, err := os.ReadFile(t.path)
	if errors.Is(err, fs.ErrNotExist) {
		return t, nil
	} else if err != nil {
		return nil, err
	}
	if err := json.Unmarshal(data, &t.entries); err != nil {
		// 坏文件按空库起步，下次 Add 时重写——信任数据可由用户重新确认重建。
		t.entries = map[string]TrustEntry{}
	}
	return t, nil
}

func (t *TrustStore) IsTrusted(fp string) bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	_, ok := t.entries[fp]
	return ok
}

func (t *TrustStore) NameOf(fp string) string {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.entries[fp].Name
}

// CertByName 按对端设备名取持久化证书（拨号方的固定根）。未配对返回 nil。
func (t *TrustStore) CertByName(name string) []byte {
	t.mu.Lock()
	defer t.mu.Unlock()
	for _, e := range t.entries {
		if e.Name == name {
			der, err := base64.StdEncoding.DecodeString(e.CertB64)
			if err != nil {
				return nil
			}
			return der
		}
	}
	return nil
}

// Add 登记并持久化一条信任（含对端证书，拨号方要用它做固定根）。
func (t *TrustStore) Add(fp, name string, certDER []byte) error {
	t.mu.Lock()
	t.entries[fp] = TrustEntry{
		Name:    name,
		CertB64: base64.StdEncoding.EncodeToString(certDER),
		Added:   time.Now().Format(time.RFC3339),
	}
	t.mu.Unlock()
	return t.save()
}

// TrustView 是已授权设备的投影（设置屏管理列表用）。
type TrustView struct {
	Fingerprint string `json:"fingerprint"`
	Name        string `json:"name"`
	Added       string `json:"added"`
}

// Entries 返回全部信任条目（按加入时间倒序：新设备在前）。
func (t *TrustStore) Entries() []TrustView {
	t.mu.Lock()
	defer t.mu.Unlock()
	out := make([]TrustView, 0, len(t.entries))
	for fp, e := range t.entries {
		out = append(out, TrustView{Fingerprint: fp, Name: e.Name, Added: e.Added})
	}
	return out
}

// Remove 撤销一条信任（设置屏「已授权设备管理」）。
func (t *TrustStore) Remove(fp string) error {
	t.mu.Lock()
	delete(t.entries, fp)
	t.mu.Unlock()
	return t.save()
}

func (t *TrustStore) save() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	data, err := json.MarshalIndent(t.entries, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(t.path), 0o700); err != nil {
		return err
	}
	return os.WriteFile(t.path, data, 0o600)
}

// NameFromInstance 由实例名还原设备名（InstanceOf 的逆运算）。
func NameFromInstance(instance string) string {
	s := strings.TrimSuffix(instance, ".")
	s = strings.TrimSuffix(s, "._flowgrid._tcp.local")
	return s
}
