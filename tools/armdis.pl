#!/usr/bin/perl
# armdis.pl: a minimal ARM-mode (A32) disassembler for a 32-bit ARM ELF:
# branches, data processing, loads and stores, enough to follow a function's
# control flow by eye. Addresses are named from the file's .symtab. Stands in
# for objdump -d.
#   perl armdis.pl <lib.so> <hex address or symbol name> [hex length]
# Without a length, the symbol's own size is used.
use strict;
my ($f, $where, $len) = @ARGV;
die "usage: armdis.pl <lib.so> <hex address or symbol> [hex length]\n" unless defined $where;
open(my $h, '<:raw', $f) or die "$f: $!\n";
my $d = do { local $/; <$h> };

# .symtab: functions by address (file offset = address for the text segment)
my ($shoff) = unpack('x32 V', $d);
my ($shentsz, $shnum, $shstr) = unpack('x46 v v v', $d);
my @sh = map { [unpack('V10', substr($d, $shoff + $_ * $shentsz, 40))] } 0 .. $shnum - 1;
my $names = substr($d, $sh[$shstr][4], $sh[$shstr][5]);
my %sec = map { unpack('Z*', substr($names, $_->[0])) => $_ } @sh;
my (%sym, %size, %addr);
if (my $sy = $sec{'.symtab'}) {
  my $st = $sec{'.strtab'};
  for (my $o = 16; $o < $sy->[5]; $o += 16) {
    my ($n, $v, $sz, $info) = unpack('V V V C', substr($d, $sy->[4] + $o, 16));
    next unless ($info & 15) == 2;
    my $nm = unpack('Z*', substr($d, $st->[4] + $n, 400));
    $sym{$v & ~1} = $nm;
    $size{$v & ~1} = $sz;
    $addr{$nm} = $v & ~1;
  }
}
my @sorted = sort { $a <=> $b } keys %sym;
my $start = exists $addr{$where} ? $addr{$where} : hex($where);
$len = defined $len ? hex($len) : ($size{$start} || 0x40);

sub name {
  my $a = shift;
  return sprintf('%x <%s>', $a, substr($sym{$a}, 0, 90)) if $sym{$a};
  my ($lo, $hi) = (0, $#sorted);
  return sprintf('%x', $a) if !@sorted || $a < $sorted[0];
  while ($lo < $hi) {
    my $m = int(($lo + $hi + 1) / 2);
    if ($sorted[$m] <= $a) { $lo = $m } else { $hi = $m - 1 }
  }
  return sprintf('%x <%s+0x%x>', $a, substr($sym{$sorted[$lo]}, 0, 90), $a - $sorted[$lo]);
}
my @cc = (qw(eq ne cs cc mi pl vs vc hi ls ge lt gt le), '', 'nv');
my @dp = qw(and eor sub rsb add adc sbc rsc tst teq cmp cmn orr mov bic mvn);
my @rn = ('r0' .. 'r9', 'sl', 'fp', 'ip', 'sp', 'lr', 'pc');

for (my $o = $start; $o < $start + $len; $o += 4) {
  my $w = unpack('V', substr($d, $o, 4));
  my $c = $cc[$w >> 28];
  my $t;
  if (($w & 0x0e000000) == 0x0a000000) {    # b, bl
    my $off = $w & 0xffffff;
    $off -= 0x1000000 if $off & 0x800000;
    $t = sprintf('b%s%s %s', ($w & 0x01000000) ? 'l' : '', $c, name($o + 8 + $off * 4));
  } elsif (($w & 0x0fffffd0) == 0x012fff10) {    # bx, blx
    $t = (($w & 0x20) ? 'blx' : 'bx') . "$c $rn[$w & 15]";
  } elsif (($w & 0x0c000000) == 0x04000000) {    # ldr, str
    my ($l, $b, $u, $p, $wb) = (($w >> 20) & 1, ($w >> 22) & 1, ($w >> 23) & 1, ($w >> 24) & 1, ($w >> 21) & 1);
    my ($n, $r) = (($w >> 16) & 15, ($w >> 12) & 15);
    my $op = ($l ? 'ldr' : 'str') . ($b ? 'b' : '') . $c;
    if ($w & 0x02000000) {
      $t = sprintf('%s %s, [%s, %s%s]', $op, $rn[$r], $rn[$n], $u ? '' : '-', $rn[$w & 15]);
    } else {
      my $imm = ($w & 0xfff) * ($u ? 1 : -1);
      if ($n == 15) {
        my $v = unpack('V', substr($d, $o + 8 + $imm, 4));
        $t = sprintf('%s %s, =0x%x', $op, $rn[$r], $v);
      } elsif ($p) {
        $t = sprintf('%s %s, [%s, #%d]%s', $op, $rn[$r], $rn[$n], $imm, $wb ? '!' : '');
      } else {
        $t = sprintf('%s %s, [%s], #%d', $op, $rn[$r], $rn[$n], $imm);
      }
    }
  } elsif (($w & 0x0e000000) == 0x08000000) {    # ldm, stm
    my @r = map { $rn[$_] } grep { $w & (1 << $_) } 0 .. 15;
    $t = sprintf('%s%s%s%s %s%s, {%s}', (($w >> 20) & 1) ? 'ldm' : 'stm', (($w >> 23) & 1) ? 'i' : 'd',
                 (($w >> 24) & 1) ? 'b' : 'a', $c, $rn[($w >> 16) & 15], (($w >> 21) & 1) ? '!' : '', join(',', @r));
  } elsif (($w & 0x0c000000) == 0 && ($w & 0x0fc000f0) != 0x00000090 && ($w >> 28) != 15) {    # data processing
    my ($op, $s, $n, $r) = (($w >> 21) & 15, ($w >> 20) & 1, ($w >> 16) & 15, ($w >> 12) & 15);
    my $o2;
    if ($w & 0x02000000) {
      my ($imm, $rot) = ($w & 0xff, (($w >> 8) & 15) * 2);
      $imm = (($imm >> $rot) | ($imm << (32 - $rot))) & 0xffffffff if $rot;
      $o2 = sprintf('#0x%x', $imm);
    } else {
      $o2 = $rn[$w & 15];
      $o2 .= sprintf(' (shift 0x%02x)', ($w >> 4) & 0xff) if ($w >> 4) & 0xff;
    }
    $t = $op >= 8 && $op <= 11 ? "$dp[$op]$c $rn[$n], $o2"
       : $op == 13 || $op == 15 ? "$dp[$op]$c" . ($s ? 's' : '') . " $rn[$r], $o2"
       :                          "$dp[$op]$c" . ($s ? 's' : '') . " $rn[$r], $rn[$n], $o2";
  } else {
    $t = sprintf('.word 0x%08x', $w);
  }
  print "\n<$sym{$o}>:\n" if $sym{$o};
  printf "%8x: %08x  %s\n", $o, $w, $t;
}
