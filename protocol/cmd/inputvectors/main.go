// 生成跨客户端的输入行为黄金向量文件。
//
// 用法：
//     cd protocol && go run ./cmd/inputvectors -w testdata/input_vectors.json
//
// 跑完之后各端测试读这份文件，把各自产出的字节与文件比对——「两端行为一致」
// 从此有机器盯着，不靠人盯。
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"

	"github.com/flowgrid/protocol"
)

func main() {
	out := flag.String("w", "testdata/input_vectors.json", "写入路径")
	flag.Parse()

	data, err := json.MarshalIndent(protocol.BuildInputVectors(), "", "  ")
	if err != nil {
		fmt.Fprintf(os.Stderr, "生成向量失败：%v\n", err)
		os.Exit(1)
	}
	data = append(data, '\n')

	if err := os.WriteFile(*out, data, 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "写 %s 失败：%v\n", *out, err)
		os.Exit(1)
	}
	fmt.Printf("已写入 %s（%d 字节）\n", *out, len(data))
}
