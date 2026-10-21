// 两模型链路选择（PROTOCOL.md §1：直连主路径、智能连接兜底）。
//
// 纯逻辑：输入策略与可用对端，输出按优先级排序的连接计划。
// 客户端（DirectLinkManager）按计划顺序尝试，首个可用模型即用。
package protocol

// PreferredLink 优先链路策略（与客户端设置 preferredLink 一致）。
type PreferredLink uint8

const (
	// PreferredIntelligentFirst 智能连接优先（先 LAN 识别，后 mDNS）。
	PreferredIntelligentFirst PreferredLink = 0
	// PreferredDirectFirst 直连优先（先 mDNS/手动，后 LAN 识别）。
	PreferredDirectFirst PreferredLink = 1
	// PreferredIntelligentOnly 仅智能连接（无 LAN 对端时明确失败，不静默降级）。
	PreferredIntelligentOnly PreferredLink = 2
)

// LinkModel 链路模型。
type LinkModel uint8

const (
	// LinkIntelligent 智能连接（LAN 私有链发现，§3.1）。
	LinkIntelligent LinkModel = 0
	// LinkDirect 直连（mDNS 发现或手动地址，§3.2）。
	LinkDirect LinkModel = 1
)

// String 返回模型名（日志/UI 用）。
func (m LinkModel) String() string {
	switch m {
	case LinkIntelligent:
		return "intelligent"
	case LinkDirect:
		return "direct"
	default:
		return "unknown"
	}
}

// LinkPlan 连接计划：按优先级排序的候选模型；Exhausted 表示无可用路径。
type LinkPlan struct {
	Order     []LinkModel
	Exhausted bool
}

// PlanLinks 根据策略与可用对端生成连接计划。
//
// mdnsPeers: mDNS 发现的对端数；lanPeers: LAN 识别到的对端数；
// hasManual: 是否有手动输入的对端地址（属直连路径）。
//
// 语义：
//   - IntelligentLink 优先：先 LAN 后直连
//   - DirectLink 优先：先直连后 LAN
//   - 仅 IntelligentLink：只排 LAN；无 LAN 对端时计划为空（Exhausted），
//     调用方明确报错，不静默降级
func PlanLinks(preferred PreferredLink, mdnsPeers, lanPeers int, hasManual bool) LinkPlan {
	directAvailable := mdnsPeers > 0 || hasManual
	lanAvailable := lanPeers > 0

	var order []LinkModel
	switch preferred {
	case PreferredIntelligentFirst:
		if lanAvailable {
			order = append(order, LinkIntelligent)
		}
		if directAvailable {
			order = append(order, LinkDirect)
		}
	case PreferredIntelligentOnly:
		if lanAvailable {
			order = append(order, LinkIntelligent)
		}
		// 不降级：无 LAN 对端时计划为空
	case PreferredDirectFirst:
		if directAvailable {
			order = append(order, LinkDirect)
		}
		if lanAvailable {
			order = append(order, LinkIntelligent)
		}
	default:
		// 未知策略按直连优先处理
		if directAvailable {
			order = append(order, LinkDirect)
		}
		if lanAvailable {
			order = append(order, LinkIntelligent)
		}
	}
	return LinkPlan{Order: order, Exhausted: len(order) == 0}
}
