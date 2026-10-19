module github.com/flowgrid/windows

go 1.25.0

require (
	github.com/Microsoft/go-winio v0.6.2
	github.com/flowgrid/protocol v0.0.0
)

require (
	golang.org/x/sys v0.10.0 // indirect
	gopkg.in/yaml.v3 v3.0.1 // indirect
)

replace github.com/flowgrid/protocol => ../protocol
