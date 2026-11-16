"""通过 ST-LINK/SWD 观察板上自主训练，不使用 UART。"""
import argparse
import json
import time
from pathlib import Path
from stlink import ROOT,STLink,symbols

FIELDS=('sequence magic version stage fatal_count heartbeat_ms sdram_ok sdram_error_addr mode map_id free_cells '
        'episodes training_target updates epsilon_permille train_wins train_losses train_draws recent_win_percent '
        'baseline_games baseline_wins baseline_losses baseline_draws eval_games eval_wins eval_losses eval_draws eval_timeouts '
        'demo_games demo_wins demo_losses demo_draws blue red phase steps outcome blue_action red_action '
        'frames spi_errors render_us update_cycles training_ms q_bytes core_hz frozen '
        'blue_bullet_active blue_bullet_position blue_bullet_direction blue_bullet_collisions '
        'red_bullet_active red_bullet_position red_bullet_direction red_bullet_collisions q_used q_evictions '
        'blue_bullet_x blue_bullet_y red_bullet_x red_bullet_y '
        'blue_active_bullets red_active_bullets blue_shots red_shots blue_cooldown_ms red_cooldown_ms '
        'eval_blue_shots eval_red_shots eval_blue_ready eval_red_ready eval_blue_bank_kills eval_red_bank_kills eval_steps '
        'fire_cooldown_ms tick_ms speed_ratio_x1000 coordinate_scale').split()

def sample(link,address):
    for _ in range(100):
        first=link.words(address,1)[0]
        if first&1:continue
        values=link.words(address,len(FIELDS));last=link.words(address,1)[0]
        if first==values[0]==last and not last&1:
            row=dict(zip(FIELDS,values))
            if row['magic']!=0x54414e4b or row['version']!=4:raise RuntimeError('Not matching tank_battle arena multi-bullet firmware')
            return row
    raise RuntimeError('No coherent snapshot')

def ready(link,address):
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        try:
            row=sample(link,address)
            if row['stage']==4:return row
        except RuntimeError:pass
        time.sleep(0.1)
    raise TimeoutError('Tank application did not initialize')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--seconds',type=float,default=30)
    parser.add_argument('--restart',action='store_true');parser.add_argument('--until-demo',action='store_true')
    parser.add_argument('--output',type=Path,default=ROOT/'logs/monitor.json');args=parser.parse_args()
    syms=symbols();rows=[];demo_seen=None
    with STLink() as link:
        ready(link,syms['g_diag'])
        if args.restart:
            previous=sample(link,syms['g_diag'])['heartbeat_ms']
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{1}}')
            deadline=time.monotonic()+30
            # 初始化需重算射击几何；确认新快照已清空，避免把重启前状态当首帧。
            while True:
                status=sample(link,syms['g_diag'])
                if status['heartbeat_ms']!=previous and status['episodes']==0 and status['q_used']==0 and status['mode']==0:break
                if time.monotonic()>deadline:raise TimeoutError('Restart did not publish an empty learning table')
                time.sleep(0.05)
        start=time.monotonic()
        while True:
            row=sample(link,syms['g_diag']);row['elapsed_s']=round(time.monotonic()-start,3);rows.append(row)
            print(json.dumps(row),flush=True)
            if row['fatal_count'] or row['spi_errors']:raise RuntimeError('Board reported an error')
            if row['mode']==3 and demo_seen is None:demo_seen=time.monotonic()
            if args.until_demo and demo_seen is not None and time.monotonic()-demo_seen>=15:break
            if row['elapsed_s']>=args.seconds:break
            time.sleep(2)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(samples=rows,final=rows[-1]),indent=2)+'\n',encoding='utf-8')
    if args.until_demo and rows[-1]['mode']!=3:raise RuntimeError('Training did not reach demonstration before timeout')

if __name__=='__main__':main()
