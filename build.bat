make clean
make FONT=ORIG
cp output/*.firm b9s_check.firm
cp b9s_check.firm f:/luma/payloads/b9s_check.firm

SLEEP 300