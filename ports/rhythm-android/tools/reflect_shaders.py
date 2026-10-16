"""Reflect the verified shader variants; reject resources without an explicit mapping.

No placeholder binding slots are allowed. The combined texture mapping is kept
in the manifest for runtime validation against the game's NVN BindTexture call.
"""
from pathlib import Path
import struct,json,subprocess
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--directory',type=Path,required=True)
parser.add_argument('--spirv-val',type=Path,required=True)
args=parser.parse_args()
P=args.directory.resolve()
VAL=args.spirv_val.resolve()

def instructions(w):
    o=5
    while o<len(w):
        n,op=w[o]>>16,w[o]&65535
        assert n and o+n<=len(w)
        yield o,op,w[o:o+n]
        o+=n

def string(words):
    return struct.pack('<'+'I'*len(words),*words).split(b'\0',1)[0].decode()

def main():
    report=[]
    for e in json.loads((P/'manifest.json').read_text()):
        key,stage=e['key'],e['stage'];path=P/(key+'.spv')
        raw=P/(key+'.raw.spv')
        if not raw.exists():raw.write_bytes(path.read_bytes())
        w=list(struct.unpack('<'+'I'*(raw.stat().st_size//4),raw.read_bytes()))
        names={};decs={};variables={};types={};entry=[];parts=[]
        for o,op,ins in instructions(w):
            if op==5:names[ins[1]]=string(ins[2:])
            if op==71:
                decs.setdefault(ins[1],{})[ins[2]]=ins[3:]
                if ins[2]==34:ins[3]=stage # Fragment resources use set 1.
            if op==59:variables[ins[2]]=(ins[1],ins[3])
            if op in (21,22,23,32):types[ins[1]]=(op,ins[2:])
            if op==15:entry.append(ins[1])
            # The translator unconditionally declares these optional abilities.
            # Remove them and require spirv-val to prove no instruction needs them.
            if op==17 and ins[1]!=1:continue
            if op==10 and string(ins[1:])=='SPV_EXT_fragment_shader_interlock':continue
            parts.extend(ins)
        assert entry==[0 if stage==0 else 4]
        output=struct.pack('<'+'I'*(5+len(parts)),*(w[:5]+parts));path.write_bytes(output)
        subprocess.run([str(VAL),'--target-env','vulkan1.1',str(path)],check=True)
        bindings=[];attributes=[]
        for var,(typ,storage) in variables.items():
            d=decs.get(var,{})
            if 33 in d:
                binding=d[33][0];name=names.get(var,'')
                if name=='support_buffer':vk_type,kind,slot,sampler=6,1,0,-1
                elif name==('vp_c3' if stage==0 else 'fp_c3'):vk_type,kind,slot,sampler=6,2,0,-1
                elif stage==1 and name=='fp_t_tcb_8':vk_type,kind,slot,sampler=1,4,0,-1
                elif stage==1 and name=='fp_t_tcb_A':vk_type,kind,slot,sampler=1,4,1,-1
                else:raise ValueError(f'No verified resource mapping: {key} {binding} {name}')
                bindings.append((stage,binding,vk_type,kind,slot,sampler,1,0))
            elif stage==0 and storage==1 and 30 in d:
                op,pt=types[typ];assert op==32
                op,t=types[pt[1]]
                if op==23:op,t=types[t[0]]
                assert op in (21,22)
                attr_type=0 if op==22 else (1 if t[1] else 2)
                attributes.append((d[30][0],attr_type))
        bindings.sort(key=lambda b:b[1]);attributes.sort()
        assert bindings and len(attributes)==(3 if stage==0 else 0)
        result=json.loads((P/(key+'.res.json')).read_text())
        caps=[1]
        header=struct.pack('<12I',0x4d4b5644,1,stage,len(bindings),len(attributes),0,len(caps),0,0,0,result['frag_out_map'] if stage else 0,0)
        metadata=header+b''.join(struct.pack('<8i',*b) for b in bindings)+b''.join(struct.pack('<2I',*a) for a in attributes)+struct.pack('<I',1)
        (P/(key+'.vkmeta')).write_bytes(metadata)
        report.append({'key':key,'stage':stage,'bindings':bindings,'attributes':attributes,'caps':caps,'spirv_validation':'PASS'})
    (P/'reflection.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
