import matplotlib.pyplot as plt
import os

def do_command_multiple(param_sets):
    # Initialize a list to store the occurrences for each parameter set
    all_occurrences = []

    # Loop through each set of parameters
    for params in param_sets:
        paul, john, george, ringo = params
        occ = {'paul': 0, 'john': 0, 'george': 0, 'ringo': 0}

        # Run the external command and redirect output to a file
        os.system(f'./build/ule {paul} {john} {george} {ringo} &> file')

        # Read the output file and count occurrences
        with open('file') as f:
            data = f.read().splitlines()
            for l in data:
                if l in occ:
                    occ[l] += 1

        # Store the occurrences for this set of parameters
        all_occurrences.append(occ)

    # Now let's plot the results
    beatles = ['Paul', 'John', 'George', 'Ringo']
    num_sets = len(param_sets)

    # Set up the figure for multiple bars per Beatle
    fig, ax = plt.subplots(figsize=(10, 6))

    # Define width of each bar
    bar_width = 0.15
    index = range(len(beatles))

    # Define colors for each parameter set
    colors = ['blue', 'green', 'red', 'purple', 'orange', 'cyan', 'magenta', 'yellow']

    # Loop through the parameter sets and plot the occurrences
    for i, occ in enumerate(all_occurrences):
        # Calculate the position of each bar group
        offset = (i - (num_sets / 2)) * bar_width  # Adjust so the bars don't overlap
        counts = [occ[beatle] for beatle in occ]
        ax.bar([x + offset for x in index], counts, bar_width, label=f'{param_sets[i]}', color=colors[i % len(colors)])


    print(all_occurrences)
    ax.set_title('Runtime of threads depending on priority')
    ax.set_xlabel('Threads')
    ax.set_ylabel('Runtime')
    ax.set_xticks(index)
    ax.set_xticklabels(beatles)
    ax.legend(title="Parameter Sets")

    # Show the plot
    plt.tight_layout()
    plt.show()

# Example usage:
# Define multiple parameter sets
param_sets = [
    (0,0,0,0),
    (-10,0,0,0),
    (-10,-1,-5,-1),
    (-10,-8,-4,-1),
    (-10,-8,-6,-8),
    (-10,-8,-2,-8)
]

# Call the function to generate the plot
do_command_multiple(param_sets)
