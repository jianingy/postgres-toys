select netblock_sub('1.0.0.0/8'::cidr, '1.0.4.0/26'::cidr);
select netblock_sub('218.88.0.0/13'::cidr, '218.94.0.0/17'::cidr);
select netblock_acc('192.168.1.0/24'::cidr, '192.168.0.0/24'::cidr);
select netblock_acc('192.168.1.0/25'::cidr, '192.168.1.128/25'::cidr);
select netblock_acc(NULL::cidr, '192.168.1.128/25'::cidr);
