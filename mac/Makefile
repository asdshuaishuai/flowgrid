XCODEGEN ?= xcodegen
# 构建产物放内置盘：沙箱化的测试 runner 打开外置卷上的测试包会阻塞
# （TCC 已放行也会卡在 open，与代码无关——见 README「已知环境坑」）。
DERIVED  ?= $(HOME)/Library/Caches/FlowGrid/DerivedData
APP      := $(DERIVED)/Build/Products/Debug/FlowGrid.app

.PHONY: bridge generate core-test ui-test build test run clean

## 重编 Go c-archive 并刷新 C 头
bridge:
	bash scripts/build-go.sh

## 由 project.yml 生成 FlowGrid.xcodeproj
generate:
	$(XCODEGEN) generate

## 纯 Swift 包测试（不依赖 Xcode 工程）
core-test:
	cd FlowGridCore && swift build && swift test
ui-test:
	cd FlowGridUI && swift build && swift test

## 构建 app（preBuildScript 自动按需构建 c-archive）
build: generate
	xcodebuild -project FlowGrid.xcodeproj -scheme FlowGrid -configuration Debug \
		-derivedDataPath $(DERIVED) -quiet build

## FFI 往返 / 会话层 / 真实 TCP 链路 等全部单元与集成测试（swift-testing）
test:
	cd FlowGridCore && swift test

## 启动 app
run: build
	open $(APP)

clean:
	rm -rf $(DERIVED) FlowGrid.xcodeproj
