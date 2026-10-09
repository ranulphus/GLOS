program Hello;
{ GLOS M4d: compiled and run inside TPX (Ctrl-F9), the Turbo Pascal 7 IDE
  that is itself a 16-bit DPMI client (RTM). Reports over COM1. }

procedure Ser(const S: string);
var
  I: Integer;
  N: Word;
begin
  for I := 1 to Length(S) do
  begin
    N := 0;
    while (Port[$3FD] and $20 = 0) and (N < 60000) do
      Inc(N);
    Port[$3F8] := Ord(S[I]);
  end;
end;

var
  I: Integer;
  Sum: LongInt;
begin
  Sum := 0;
  for I := 1 to 100 do
    Sum := Sum + I;
  if Sum = 5050 then
    Ser('HX-TEST tpx-run ok sum=5050'#13#10)
  else
    Ser('HX-TEST tpx-run FAIL'#13#10);
end.
