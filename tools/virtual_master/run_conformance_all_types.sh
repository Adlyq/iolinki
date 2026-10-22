#!/bin/bash
# Run conformance tests with different M-sequence types

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "Running Conformance Tests - All M-Sequence Types"
echo "=========================================="

# Test configurations: M_SEQ_TYPE PD_IN_LEN PD_OUT_LEN
CONFIGS=(
    "0 0 0"      # TYPE_0 - ISDU only
    "2 0 0"      # TYPE_1_2 - 2 OD octets, no PD
    "3 0 0"      # TYPE_1_V - 8 OD octets, no PD
    "5 2 0"      # TYPE_2_2 - 2 PD-in, 1 OD
    "6 2 2"      # TYPE_2_V - variable PD, 2 OD
)

TOTAL_PASSED=0
TOTAL_FAILED=0

for config in "${CONFIGS[@]}"; do
    read -r m_type pd_in pd_out <<< "$config"

    echo ""
    echo "=========================================="
    echo -e "${YELLOW}Testing M-Sequence Type $m_type (PD_In=$pd_in, PD_Out=$pd_out)${NC}"
    echo "=========================================="

    export IOLINK_M_SEQ_TYPE="$m_type"
    export IOLINK_PD_IN_LEN="$pd_in"
    export IOLINK_PD_OUT_LEN="$pd_out"

    # Run ISDU conformance tests
    if python3 "$SCRIPT_DIR/test_conformance_isdu.py" 2>&1 | tee /tmp/conformance_"${m_type}".log; then
        echo -e "${GREEN}✅ Type $m_type: PASSED${NC}"
        ((TOTAL_PASSED++))
    else
        echo -e "${RED}❌ Type $m_type: FAILED${NC}"
        ((TOTAL_FAILED++))
    fi
done

echo ""
echo "=========================================="
echo "Conformance Test Summary"
echo "=========================================="
echo -e "Passed: ${GREEN}$TOTAL_PASSED${NC}"
echo -e "Failed: ${RED}$TOTAL_FAILED${NC}"
echo "=========================================="

if [ $TOTAL_FAILED -eq 0 ]; then
    echo -e "${GREEN}✅ All conformance tests passed!${NC}"
    exit 0
else
    echo -e "${RED}❌ Some conformance tests failed${NC}"
    exit 1
fi
