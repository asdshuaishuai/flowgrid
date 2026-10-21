package protocol

import "testing"

func TestPlanLinks(t *testing.T) {
	tests := []struct {
		name      string
		preferred PreferredLink
		mdns      int
		lan       int
		manual    bool
		want      []LinkModel
		exhausted bool
	}{
		{
			name:      "intelligent first, both available",
			preferred: PreferredIntelligentFirst,
			mdns:      1, lan: 1,
			want: []LinkModel{LinkIntelligent, LinkDirect},
		},
		{
			name:      "direct first, both available",
			preferred: PreferredDirectFirst,
			mdns:      1, lan: 1,
			want: []LinkModel{LinkDirect, LinkIntelligent},
		},
		{
			name:      "intelligent first, only lan",
			preferred: PreferredIntelligentFirst,
			mdns:      0, lan: 2,
			want:      []LinkModel{LinkIntelligent},
		},
		{
			name:      "intelligent first, only mdns",
			preferred: PreferredIntelligentFirst,
			mdns:      1, lan: 0,
			want:      []LinkModel{LinkDirect},
		},
		{
			name:      "direct first, only manual address",
			preferred: PreferredDirectFirst,
			mdns:      0, lan: 0, manual: true,
			want:      []LinkModel{LinkDirect},
		},
		{
			name:      "intelligent only, lan available",
			preferred: PreferredIntelligentOnly,
			mdns:      5, lan: 1, manual: true,
			want:      []LinkModel{LinkIntelligent},
		},
		{
			name:      "intelligent only, no lan peers",
			preferred: PreferredIntelligentOnly,
			mdns:      3, lan: 0, manual: true,
			want:      nil,
			exhausted: true,
		},
		{
			name:      "nothing available",
			preferred: PreferredDirectFirst,
			mdns:      0, lan: 0,
			want:      nil,
			exhausted: true,
		},
		{
			name:      "unknown strategy falls back to direct first",
			preferred: PreferredLink(99),
			mdns:      1, lan: 1,
			want:      []LinkModel{LinkDirect, LinkIntelligent},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			plan := PlanLinks(tt.preferred, tt.mdns, tt.lan, tt.manual)
			if plan.Exhausted != tt.exhausted {
				t.Fatalf("Exhausted = %v, want %v (order=%v)", plan.Exhausted, tt.exhausted, plan.Order)
			}
			if len(plan.Order) != len(tt.want) {
				t.Fatalf("Order = %v, want %v", plan.Order, tt.want)
			}
			for i := range tt.want {
				if plan.Order[i] != tt.want[i] {
					t.Fatalf("Order[%d] = %v, want %v (full=%v)", i, plan.Order[i], tt.want[i], plan.Order)
				}
			}
		})
	}
}

func TestLinkModelString(t *testing.T) {
	if LinkIntelligent.String() != "intelligent" {
		t.Fatalf("LinkIntelligent.String() = %q", LinkIntelligent.String())
	}
	if LinkDirect.String() != "direct" {
		t.Fatalf("LinkDirect.String() = %q", LinkDirect.String())
	}
}
