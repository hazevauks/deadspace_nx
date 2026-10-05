#!/usr/bin/perl
# dexinfo.pl: the classes of a .dex whose name matches a pattern, with their
# fields and methods (flags, name, JNI signature). Stands in for dexdump.
#   perl dexinfo.pl <classes.dex> <class regex> [native]
# With "native", only the native methods are printed. With "code", each method
# is followed by what its bytecode refers to, in order: strings, fields read
# or written, methods called. A fourth argument filters methods by name.
use strict;
use warnings;

my ($path, $pat, $only, $mpat) = @ARGV;
die "usage: dexinfo.pl <classes.dex> <class regex> [native]\n" unless $path && $pat;
$only ||= '';

open(my $fh, '<:raw', $path) or die "$path: $!\n";
my $d = do { local $/; <$fh> };
close($fh);
die "not a dex\n" unless substr($d, 0, 4) eq "dex\n";

my ($string_n, $string_off, $type_n, $type_off, $proto_n, $proto_off,
    $field_n, $field_off, $method_n, $method_off, $class_n, $class_off) =
  unpack('V12', substr($d, 0x38, 48));

sub uleb {
  my ($pos) = @_;
  my ($v, $shift) = (0, 0);
  while (1) {
    my $b = ord(substr($d, $$pos++, 1));
    $v |= ($b & 0x7f) << $shift;
    last unless $b & 0x80;
    $shift += 7;
  }
  return $v;
}

my @str_cache;
sub str {
  my ($i) = @_;
  return $str_cache[$i] if defined $str_cache[$i];
  my $pos = unpack('V', substr($d, $string_off + $i * 4, 4));
  uleb(\$pos);
  my $end = index($d, "\0", $pos);
  return $str_cache[$i] = substr($d, $pos, $end - $pos);
}
sub type { my ($i) = @_; return str(unpack('V', substr($d, $type_off + $i * 4, 4))); }
sub proto {
  my ($i) = @_;
  my ($shorty, $ret, $params) = unpack('V3', substr($d, $proto_off + $i * 12, 12));
  my $sig = '(';
  if ($params) {
    my $n = unpack('V', substr($d, $params, 4));
    $sig .= type($_) for unpack("v$n", substr($d, $params + 4, $n * 2));
  }
  return $sig . ')' . type($ret);
}

sub flags {
  my ($f) = @_;
  my @n;
  push @n, 'public' if $f & 0x1;
  push @n, 'private' if $f & 0x2;
  push @n, 'protected' if $f & 0x4;
  push @n, 'static' if $f & 0x8;
  push @n, 'native' if $f & 0x100;
  push @n, 'abstract' if $f & 0x400;
  return join(' ', @n);
}

# Instruction lengths in 16-bit units, by opcode (Dalvik's instruction formats).
my @len = (1) x 256;
$len[$_] = 2 for 0x02, 0x05, 0x08, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c, 0x1f, 0x20, 0x22, 0x23, 0x29,
                 0x2d .. 0x3d, 0x44 .. 0x6d, 0x90 .. 0xaf, 0xd0 .. 0xe2, 0xfe, 0xff;
$len[$_] = 3 for 0x03, 0x06, 0x09, 0x14, 0x17, 0x1b, 0x24, 0x25, 0x26, 0x2a, 0x2b, 0x2c,
                 0x6e .. 0x72, 0x74 .. 0x78, 0xfc, 0xfd;
$len[0x18] = 5;
$len[$_] = 4 for 0xfa, 0xfb;

sub field_ref {
  my ($i) = @_;
  my ($fc, $ft, $fn) = unpack('v v V', substr($d, $field_off + $i * 8, 8));
  return type($fc) . '->' . str($fn);
}
sub method_ref {
  my ($i) = @_;
  my ($mc, $mp, $mn) = unpack('v v V', substr($d, $method_off + $i * 8, 8));
  return type($mc) . '->' . str($mn) . ' ' . proto($mp);
}

sub code_refs {
  my ($off) = @_;
  my $n = unpack('V', substr($d, $off + 12, 4));
  my @u = unpack("v$n", substr($d, $off + 16, $n * 2));
  my @out;
  my $i = 0;
  while ($i < $n) {
    my $op = $u[$i] & 0xff;
    my $hi = $u[$i] >> 8;
    if ($op == 0 && $hi) {
      # the data after a switch or fill-array-data: skipped
      my $size = $u[$i + 1];
      if ($hi == 1) { $i += $size * 2 + 4; }
      elsif ($hi == 2) { $i += $size * 4 + 2; }
      elsif ($hi == 3) { my $count = $u[$i + 2] | ($u[$i + 3] << 16); $i += int(($size * $count + 1) / 2) + 4; }
      else { $i++; }
      next;
    }
    if ($op == 0x1a) { push @out, '      "' . str($u[$i + 1]) . '"'; }
    elsif ($op == 0x1b) { push @out, '      "' . str($u[$i + 1] | ($u[$i + 2] << 16)) . '"'; }
    elsif ($op >= 0x12 && $op <= 0x14) {
      my $v = $op == 0x12 ? ($hi >> 4) : $op == 0x13 ? $u[$i + 1] : ($u[$i + 1] | ($u[$i + 2] << 16));
      push @out, "      const $v";
    }
    elsif ($op >= 0x60 && $op <= 0x66) { push @out, '      sget ' . field_ref($u[$i + 1]); }
    elsif ($op >= 0x67 && $op <= 0x6d) { push @out, '      sput ' . field_ref($u[$i + 1]); }
    elsif ($op >= 0x52 && $op <= 0x58) { push @out, '      iget ' . field_ref($u[$i + 1]); }
    elsif ($op >= 0x59 && $op <= 0x5f) { push @out, '      iput ' . field_ref($u[$i + 1]); }
    elsif (($op >= 0x6e && $op <= 0x72) || ($op >= 0x74 && $op <= 0x78)) {
      push @out, '      call ' . method_ref($u[$i + 1]);
    }
    elsif ($op >= 0x32 && $op <= 0x3d) { push @out, '      if'; }
    elsif ($op == 0x82) { push @out, '      int-to-float'; }
    elsif ($op >= 0xa6 && $op <= 0xa9) { push @out, '      ' . (qw(add sub mul div))[$op - 0xa6] . '-float'; }
    elsif ($op >= 0xc6 && $op <= 0xc9) { push @out, '      ' . (qw(add sub mul div))[$op - 0xc6] . '-float'; }
    $i += $len[$op];
  }
  return @out;
}

for my $c (0 .. $class_n - 1) {
  my ($cls, $acc, $super, $ifaces, $src, $annot, $data_off, $static_off) =
    unpack('V8', substr($d, $class_off + $c * 32, 32));
  my $name = type($cls);
  next unless $name =~ /$pat/;
  my @out;
  if ($data_off) {
    my $pos = $data_off;
    my ($sf, $if, $dm, $vm) = map { uleb(\$pos) } 1 .. 4;
    for my $group ([$sf, 'static'], [$if, '']) {
      my $idx = 0;
      for (1 .. $group->[0]) {
        $idx += uleb(\$pos);
        my $f = uleb(\$pos);
        my ($fc, $ft, $fn) = unpack('v v V', substr($d, $field_off + $idx * 8, 8));
        push @out, sprintf("  field  %-24s %s %s", flags($f), str($fn), type($ft)) unless $only;
      }
    }
    for my $count ($dm, $vm) {
      my $idx = 0;
      for (1 .. $count) {
        $idx += uleb(\$pos);
        my $f = uleb(\$pos);
        my $code = uleb(\$pos);
        my ($mc, $mp, $mn) = unpack('v v V', substr($d, $method_off + $idx * 8, 8));
        next if $only eq 'native' && !($f & 0x100);
        next if $only eq "code" && defined $mpat && str($mn) !~ /$mpat/;
        push @out, sprintf("  method %-24s %s %s", flags($f), str($mn), proto($mp));
        push @out, code_refs($code) if $only eq "code" && $code;
      }
    }
  }
  next if $only && !@out;
  my $sup = $super == 0xffffffff ? '' : type($super);
  print "$name extends $sup\n", map { "$_\n" } @out;
}
