# plot_metrics.py
# Usage: python plot_metrics.py metrics.csv
#
# Expects CSV header:
# episode,reward,cumulative_goals,success_rate,training_loss,steps
#
# Produces plots in plots/metrics_plot.png and shows them.

import sys
import os
import csv
import math
import numpy as np
import matplotlib.pyplot as plt

WINDOW = 20

def pad_to_length(lst, length, pad_value=np.nan):
    if lst is None:
        return [pad_value] * length
    if len(lst) >= length:
        return list(lst[:length])
    return list(lst) + [pad_value] * (length - len(lst))

def rolling_mean_nan(arr, window):
    arr = np.asarray(arr, dtype=float)
    n = len(arr)
    if n == 0:
        return np.array([])
    out = np.full(n, np.nan)
    for i in range(n):
        start = max(0, i - window + 1)
        win = arr[start:i + 1]
        if np.isnan(win).all():
            out[i] = np.nan
        else:
            out[i] = np.nanmean(win)
    return out

def first_success_episode(cumulative_goals_or_goals):
    # If cumulative_goals given, first episode where cumulative increases
    if cumulative_goals_or_goals is None or len(cumulative_goals_or_goals) == 0:
        return None
    arr = np.asarray(cumulative_goals_or_goals)
    # If values are 0/1 goals, convert to cumulative
    if np.all((arr == 0) | (arr == 1)):
        for i, v in enumerate(arr):
            if v == 1:
                return i + 1
        return None
    # otherwise assume cumulative: find first index where value > 0
    for i, v in enumerate(arr):
        if v > 0:
            return i + 1
    return None

def maybe_percent(series):
    if series is None or len(series) == 0:
        return series
    arr = np.asarray(series, dtype=float)
    if np.nanmax(arr) <= 1.0:
        return (arr * 100.0).tolist()
    return series

def load_metrics(path):
    ep = []
    reward = []
    cum_goals = []
    success = []
    loss = []
    steps = []

    with open(path, newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # guard against missing columns
            ep.append(int(row.get('episode', len(ep))))
            reward.append(float(row.get('reward', 'nan') or math.nan))
            cum_goals.append(float(row.get('cumulative_goals', 'nan') or math.nan))
            success.append(float(row.get('success_rate', 'nan') or math.nan))
            loss.append(float(row.get('training_loss', 'nan') or math.nan))
            steps.append(float(row.get('steps', 'nan') or math.nan))

    return {
        "episode": np.array(ep, dtype=int),
        "reward": np.array(reward, dtype=float),
        "cumulative_goals": np.array(cum_goals, dtype=float),
        "success_rate": np.array(success, dtype=float),
        "training_loss": np.array(loss, dtype=float),
        "steps": np.array(steps, dtype=float),
    }

def plot_metrics(metrics, base_name="metrics"):
    episodes_len = len(metrics["episode"])
    episodes_range = np.arange(1, episodes_len + 1)

    # Prepare series and windows
    b_rewards = pad_to_length(metrics["reward"].tolist(), episodes_len, np.nan)
    b_losses = pad_to_length(metrics["training_loss"].tolist(), episodes_len, np.nan)
    b_success = pad_to_length(metrics["success_rate"].tolist(), episodes_len, 0)
    b_success = maybe_percent(b_success)
    b_cumgoals = pad_to_length(metrics["cumulative_goals"].tolist(), episodes_len, 0)
    b_steps = pad_to_length(metrics["steps"].tolist(), episodes_len, np.nan)

    # Rolling means
    reward_ma = rolling_mean_nan(b_rewards, WINDOW)
    loss_ma = rolling_mean_nan(b_losses, WINDOW)
    success_ma = rolling_mean_nan(b_success, WINDOW)
    steps_ma = rolling_mean_nan(b_steps, WINDOW)

    # Figure layout similar to worldcist's comparison (3x2)
    fig = plt.figure(figsize=(16, 12))
    gs = fig.add_gridspec(3, 2, hspace=0.3, wspace=0.3)

    # (a) Cumulative Reward
    ax1 = fig.add_subplot(gs[0, 0])
    ax1.plot(episodes_range, b_rewards, alpha=0.15, linewidth=0.6, color='tab:blue')
    ax1.plot(episodes_range, reward_ma, label='Reward (MA)', color='tab:blue', linewidth=2)
    ax1.set_xlabel('Episode')
    ax1.set_ylabel('Cumulative Reward')
    ax1.set_title('(a) Cumulative Reward per Episode')
    ax1.grid(True, alpha=0.3)
    ax1.legend()

    # (b) Success Rate (window)
    ax2 = fig.add_subplot(gs[0, 1])
    ax2.plot(episodes_range, success_ma, label='Success Rate (MA)', color='tab:green', linewidth=2)
    ax2.set_xlabel('Episode')
    ax2.set_ylabel('Success Rate (%)')
    ax2.set_title('(b) Success Rate (rolling window)')
    ax2.set_ylim([0, 105])
    ax2.grid(True, alpha=0.3)
    ax2.legend()

    # (c) Cumulative Goals
    ax3 = fig.add_subplot(gs[1, 0])
    ax3.plot(episodes_range, b_cumgoals, label='Cumulative Goals', color='tab:orange', linewidth=2)
    ax3.set_xlabel('Episode')
    ax3.set_ylabel('Cumulative Goals')
    ax3.set_title('(c) Cumulative Goals Reached')
    ax3.grid(True, alpha=0.3)
    ax3.legend()

    # (d) Training Loss
    ax4 = fig.add_subplot(gs[1, 1])
    ax4.plot(episodes_range, loss_ma, label='Loss (MA)', color='tab:purple', linewidth=2)
    ax4.set_xlabel('Episode')
    ax4.set_ylabel('TD Loss')
    ax4.set_title('(d) Training Loss')
    try:
        ax4.set_yscale('log')
    except Exception:
        pass
    ax4.grid(True, alpha=0.3, which='both')
    ax4.legend()

    # (e) Episode Steps
    ax5 = fig.add_subplot(gs[2, 0])
    ax5.plot(episodes_range, steps_ma, label='Steps (MA)', color='tab:cyan', linewidth=2)
    ax5.set_xlabel('Episode')
    ax5.set_ylabel('Steps per Episode')
    ax5.set_title('(e) Episode Length (efficiency)')
    ax5.grid(True, alpha=0.3)
    ax5.legend()

    # (f) Raw success (binary) + first-success marker
    ax6 = fig.add_subplot(gs[2, 1])
    # derive per-episode binary success from cumulative_goals when possible
    cum = np.asarray(b_cumgoals, dtype=float)
    if not np.isnan(cum).all():
        # convert cumulative to per-episode goals if cumulative is non-decreasing
        per_episode_goals = np.empty_like(cum)
        per_episode_goals[0] = cum[0]
        per_episode_goals[1:] = cum[1:] - cum[:-1]
        per_episode_goals = np.clip(per_episode_goals, 0, 1)
    else:
        per_episode_goals = np.zeros(episodes_len)
    ax6.plot(episodes_range, per_episode_goals, drawstyle='steps-mid', label='Goal (binary)', color='tab:red', linewidth=1.5)
    ax6.set_xlabel('Episode')
    ax6.set_ylabel('Goal Reached (0/1)')
    ax6.set_title('(f) Goal per Episode and First Success')
    ax6.set_ylim(-0.1, 1.1)
    ax6.grid(True, alpha=0.3)
    ax6.legend()

    # First success annotation
    first = first_success_episode(cum.tolist() if not np.isnan(cum).all() else per_episode_goals.tolist())
    if first is not None:
        for ax in [ax1, ax2, ax3, ax4, ax5, ax6]:
            ax.axvline(first, color='k', linestyle='--', alpha=0.6, linewidth=1.0)
        ax6.annotate(f'First success (ep {first})', xy=(first, 0.5), xytext=(first + max(1, episodes_len//50), 0.7),
                     arrowprops=dict(arrowstyle='-|>', color='k', alpha=0.6), fontsize=9)

    # Save & show
    os.makedirs("plots", exist_ok=True)
    fname = f"plots/{base_name}_metrics_plot.png"
    plt.savefig(fname, dpi=300, bbox_inches='tight')
    print(f"[INFO] Saved plot: {fname}")
    plt.show()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: python plot_metrics.py metrics.csv")
        sys.exit(1)

    metrics_path = sys.argv[1]
    data = load_metrics(metrics_path)
    base = os.path.splitext(os.path.basename(metrics_path))[0]
    plot_metrics(data, base_name=base)